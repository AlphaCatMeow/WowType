//! 从用户自己的输入里学：选过的词有多「热」，哪两个词常挨着用。
//!
//! 照着微信输入法的思路（`SelectedHistory`、`history_hot_val`、`context_bigram_score`）：
//!
//! - 热度：每选一次加 1，按 30 天半衰期衰减（WeType 超参数里的 2592000 秒）。
//!   排序用 ln(1+热度)，选得越多越靠前，但涨得越来越慢；很久不用就慢慢退回去。
//! - 搭配：用户上屏时记下「上一个词 -> 这个词」（同一句里相邻的词，以及上一次上屏的
//!   最后一个词 -> 这次的第一个词）。束搜索给这些搭配加分，所以 `fagei` 选过一次
//!   「发给」以后，`wofageini` 出「我发给你」而不是「我发个你」。
//!
//! 都存在 `history.tsv`（和用户词库放一起），重启输入法不会丢。

use std::collections::{HashMap, VecDeque};
use std::path::Path;

use anyhow::{Context, Result};

use crate::lm::LanguageModel;
use crate::HistoryEntry;

/// 半衰期：30 天（毫秒）。
pub const HALF_LIFE_MS: f64 = 30.0 * 24.0 * 3600.0 * 1000.0;

/// 一个学过的搭配给多少分（自然对数）：`PAIR_WEIGHT * ln(1+热度)`，最多 [`PAIR_BONUS_CAP`]。
///
/// 用过一次约 1.7，三次约 3.5——足够让常见的同音搭配（发给/发个、再/在）翻过来，
/// 又不至于让一次误选永远压住词库里差得很远的词。
pub const PAIR_WEIGHT: f64 = 2.5;
pub const PAIR_BONUS_CAP: f64 = 6.0;

/// 选词热度衰减到这么低（相当于不到半次）就不再让候选提前（[`crate::Engine`] 的「上次选过的放第一」）：
/// 误选一次的词 30 天以后不再一直顶在第一个。
pub const MIN_PICK_HEAT: f64 = 0.5;

/// 存盘时热度低于这个的记录不再写（选过一次的约 150 天）：排序里已经几乎不起作用，留着只让文件越来越大。
pub const PRUNE_HEAT: f64 = 1.0 / 32.0;

/// 分几次选出来拼成的词第一次只记成「候选新词」，这么久以内再拼一次才收进用户词库（30 天，毫秒）。
pub const NEW_WORD_TTL_MS: u64 = 30 * 24 * 3600 * 1000;

/// 按半衰期衰减后的次数（热度）。
pub fn decayed(entry: HistoryEntry, at_ms: u64) -> f64 {
    let age = at_ms.saturating_sub(entry.last_used_ms) as f64;
    entry.count * 0.5f64.powf(age / HALF_LIFE_MS)
}

/// 先按时间衰减再加 1（很久以前用得多的不会一直顶着），不取整：取整会把半次凑成一次。
pub fn bumped(before: Option<HistoryEntry>, at_ms: u64) -> HistoryEntry {
    HistoryEntry {
        count: before.map_or(0.0, |e| decayed(e, at_ms)) + 1.0,
        last_used_ms: at_ms,
    }
}

/// 分几次选出来拼成、还没收进用户词库的词（「候选新词」）。
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NewWord {
    pub pinyin: String,
    /// 第一次拼出来的时间（epoch 毫秒）。
    pub at_ms: u64,
}

/// 一次学习改了什么：改之前、改之后的值（None 是没有这一条）。撤销时只在现在的值还等于「之后」时
/// 才改回「之前」，后来又被别的学习改过的不动。
#[derive(Clone, Debug, PartialEq)]
pub enum Change {
    /// 选词热度（history.tsv 的 w 行）。
    Word {
        word: String,
        before: Option<HistoryEntry>,
        after: Option<HistoryEntry>,
    },
    /// 词搭配（p 行）。
    Pair {
        prev: String,
        cur: String,
        before: Option<HistoryEntry>,
        after: Option<HistoryEntry>,
    },
    /// 最近一次选的词（接下一次的搭配用）。
    LastWord { before: String, after: String },
    /// 用户词库里的词：(拼音, 频次)。
    UserWord {
        word: String,
        before: Option<(String, f64)>,
        after: Option<(String, f64)>,
    },
    /// 候选新词（n 行）。
    NewWord {
        word: String,
        before: Option<NewWord>,
        after: Option<NewWord>,
    },
}

/// 学习日志最多留几条改动（一次上屏一般十来条，够撤销最近几次）。
pub const JOURNAL_MAX: usize = 1024;

/// 最近的学习改动，按顺序编号：撤销最近一次学习时按编号区间退回去，不用重算全部。
#[derive(Debug, Default)]
pub struct Journal {
    /// changes[0] 的编号。
    first: u64,
    /// 撤销过的是 None（编号不变）。
    changes: VecDeque<Option<Change>>,
}

impl Journal {
    /// 下一条改动的编号：学习前后各取一次，中间就是这次学习改的。
    pub fn mark(&self) -> u64 {
        self.first + self.changes.len() as u64
    }

    pub fn push(&mut self, change: Change) {
        self.changes.push_back(Some(change));
        while self.changes.len() > JOURNAL_MAX {
            self.changes.pop_front();
            self.first += 1;
        }
    }

    /// 取出编号在 [start, end) 里、还没撤销的改动，新的在前。start 那条已经挤出日志时返回 None
    /// （只退一半不如不退）。
    pub fn take(&mut self, start: u64, end: u64) -> Option<Vec<Change>> {
        if start < self.first {
            return None;
        }
        let end = end.min(self.mark());
        let mut out = Vec::new();
        for seq in (start..end).rev() {
            if let Some(change) = self.changes[(seq - self.first) as usize].take() {
                out.push(change);
            }
        }
        Some(out)
    }
}

/// 用户学到的词搭配：前一个词 -> 后一个词 -> 次数与上次使用时间。
#[derive(Default, Debug, Clone)]
pub struct UserPairs {
    map: HashMap<String, HashMap<String, HistoryEntry>>,
}

impl UserPairs {
    /// 记一次 prev -> cur，返回改动（撤销用）；有一个是空的什么也不记。
    pub fn learn(&mut self, prev: &str, cur: &str, at_ms: u64) -> Option<Change> {
        if prev.is_empty() || cur.is_empty() {
            return None;
        }
        // 次数先按时间衰减再加 1，这样很久以前用过很多次的搭配不会一直顶着
        let before = self.get(prev, cur);
        let after = bumped(before, at_ms);
        self.insert(prev.to_string(), cur.to_string(), after);
        Some(Change::Pair {
            prev: prev.to_string(),
            cur: cur.to_string(),
            before,
            after: Some(after),
        })
    }

    /// 改成 entry（None 是去掉这一条），撤销用。
    pub fn set(&mut self, prev: &str, cur: &str, entry: Option<HistoryEntry>) {
        match entry {
            Some(entry) => self.insert(prev.to_string(), cur.to_string(), entry),
            None => {
                if let Some(next) = self.map.get_mut(prev) {
                    next.remove(cur);
                    if next.is_empty() {
                        self.map.remove(prev);
                    }
                }
            }
        }
    }

    /// prev 后面学过的词和它们的热度（at_ms 时），热的在前。
    pub fn next_words(&self, prev: &str, at_ms: u64) -> Vec<(String, f64)> {
        let mut out: Vec<(String, f64)> = self
            .map
            .get(prev)
            .map(|next| {
                next.iter()
                    .map(|(cur, entry)| (cur.clone(), decayed(*entry, at_ms)))
                    .collect()
            })
            .unwrap_or_default();
        out.sort_by(|a, b| b.1.total_cmp(&a.1).then_with(|| a.0.cmp(&b.0)));
        out
    }

    pub fn get(&self, prev: &str, cur: &str) -> Option<HistoryEntry> {
        self.map.get(prev).and_then(|next| next.get(cur)).copied()
    }

    /// 这个搭配的加分（没学过是 0）。
    pub fn bonus(&self, prev: &str, cur: &str, at_ms: u64) -> f64 {
        match self.get(prev, cur) {
            Some(entry) => (PAIR_WEIGHT * (1.0 + decayed(entry, at_ms)).ln()).min(PAIR_BONUS_CAP),
            None => 0.0,
        }
    }

    /// 去掉和 word 有关的搭配（它在前或在后的都去掉），返回去掉了几条。
    pub fn remove_word(&mut self, word: &str) -> usize {
        let mut removed = self.map.remove(word).map_or(0, |next| next.len());
        for next in self.map.values_mut() {
            removed += usize::from(next.remove(word).is_some());
        }
        self.map.retain(|_, next| !next.is_empty());
        removed
    }

    pub fn len(&self) -> usize {
        self.map.values().map(HashMap::len).sum()
    }

    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }

    pub fn iter(&self) -> impl Iterator<Item = (&str, &str, HistoryEntry)> {
        self.map.iter().flat_map(|(prev, next)| {
            next.iter()
                .map(move |(cur, entry)| (prev.as_str(), cur.as_str(), *entry))
        })
    }

    fn insert(&mut self, prev: String, cur: String, entry: HistoryEntry) {
        self.map.entry(prev).or_default().insert(cur, entry);
    }
}

/// 基础语言模型 + 用户学到的搭配。束搜索用它打分。
///
/// `start` 是光标前最后一个词（上一次上屏的），句首的第一个词用它查搭配。
pub struct LearnedLm<'a> {
    pub base: &'a dyn LanguageModel,
    pub pairs: &'a UserPairs,
    pub start: Option<&'a str>,
    pub at_ms: u64,
    /// 光标前已经有字（接着前文打）：第一个词不是句首，不加基础模型的句首分。
    pub mid_sentence: bool,
}

impl LanguageModel for LearnedLm<'_> {
    fn score(&self, prev: Option<&str>, cur: &str) -> f64 {
        let base = if prev.is_none() && self.mid_sentence {
            0.0
        } else {
            self.base.score(prev, cur)
        };
        match prev.or(self.start) {
            Some(prev) => base + self.pairs.bonus(prev, cur, self.at_ms),
            None => base,
        }
    }

    fn has_bigram(&self, prev: &str, cur: &str) -> bool {
        self.base.has_bigram(prev, cur) || self.pairs.get(prev, cur).is_some()
    }

    fn top_next(&self, prev: &str, limit: usize) -> Vec<(String, f64)> {
        self.base.top_next(prev, limit)
    }
}

/// 存盘格式（TSV，`#` 开头是注释，各列用 tab 分隔）：
///
/// ```text
/// w <词> <热度> <上次使用，epoch 毫秒>
/// p <前一个词> <后一个词> <热度> <上次使用>
/// n <候选新词> <拼音> <第一次拼出来的时间>
/// ```
///
/// 热度是上次使用时衰减过的次数，可以带小数（最多三位；旧版本只写整数，读得进来）。
/// now_ms 时热度已经低于 [`PRUNE_HEAT`] 的、过了 [`NEW_WORD_TTL_MS`] 的候选新词不写。
pub fn history_text(
    words: &HashMap<String, HistoryEntry>,
    pairs: &UserPairs,
    new_words: &HashMap<String, NewWord>,
    now_ms: u64,
) -> String {
    let mut out = String::from("# wowtype 用户输入习惯 v1\n");
    let clean = |s: &str| !s.is_empty() && !s.contains(['\t', '\n', '\r']);
    let live = |e: &HistoryEntry| decayed(*e, now_ms) >= PRUNE_HEAT;
    let mut words: Vec<_> = words.iter().filter(|(w, e)| clean(w) && live(e)).collect();
    words.sort_by(|a, b| a.0.cmp(b.0));
    for (word, entry) in words {
        out.push_str(&format!(
            "w\t{word}\t{}\t{}\n",
            heat_text(entry.count),
            entry.last_used_ms
        ));
    }
    let mut pairs: Vec<_> = pairs
        .iter()
        .filter(|(p, c, e)| clean(p) && clean(c) && live(e))
        .collect();
    pairs.sort_by(|a, b| (a.0, a.1).cmp(&(b.0, b.1)));
    for (prev, cur, entry) in pairs {
        out.push_str(&format!(
            "p\t{prev}\t{cur}\t{}\t{}\n",
            heat_text(entry.count),
            entry.last_used_ms
        ));
    }
    let mut new_words: Vec<_> = new_words
        .iter()
        .filter(|(w, n)| {
            clean(w) && clean(&n.pinyin) && now_ms.saturating_sub(n.at_ms) <= NEW_WORD_TTL_MS
        })
        .collect();
    new_words.sort_by(|a, b| a.0.cmp(b.0));
    for (word, new) in new_words {
        out.push_str(&format!("n\t{word}\t{}\t{}\n", new.pinyin, new.at_ms));
    }
    out
}

/// 热度写成文字：整数照旧写整数，否则最多三位小数。
fn heat_text(count: f64) -> String {
    if (count - count.round()).abs() < 1e-6 {
        return format!("{}", count.round().max(0.0) as u64);
    }
    let text = format!("{count:.3}");
    text.trim_end_matches('0').trim_end_matches('.').to_string()
}

/// 把 [`history_text`] 整份写进 path。
pub fn save_history<P: AsRef<Path>>(
    path: P,
    words: &HashMap<String, HistoryEntry>,
    pairs: &UserPairs,
    new_words: &HashMap<String, NewWord>,
    now_ms: u64,
) -> Result<()> {
    let text = history_text(words, pairs, new_words, now_ms);
    // 先写临时文件再改名，写到一半断电也不会把旧文件弄坏
    crate::fsutil::write_atomic(path.as_ref(), text.as_bytes())
}

/// 读回 [`save_history`] 写的文件，返回读到的条数。坏行跳过。
pub fn load_history<P: AsRef<Path>>(
    path: P,
    words: &mut HashMap<String, HistoryEntry>,
    pairs: &mut UserPairs,
    new_words: &mut HashMap<String, NewWord>,
) -> Result<usize> {
    let path = path.as_ref();
    let text = std::fs::read_to_string(path).with_context(|| format!("读取 {}", path.display()))?;
    let mut loaded = 0usize;
    for line in text.lines() {
        if line.starts_with('#') || line.trim().is_empty() {
            continue;
        }
        let parts: Vec<&str> = line.split('\t').collect();
        let entry = |count: &str, last: &str| -> Option<HistoryEntry> {
            let count: f64 = count.parse().ok()?;
            (count.is_finite() && count >= 0.0).then_some(())?;
            Some(HistoryEntry {
                count,
                last_used_ms: last.parse().ok()?,
            })
        };
        match parts.as_slice() {
            ["w", word, count, last] if !word.is_empty() => {
                if let Some(e) = entry(count, last) {
                    words.insert((*word).to_string(), e);
                    loaded += 1;
                }
            }
            ["p", prev, cur, count, last] if !prev.is_empty() && !cur.is_empty() => {
                if let Some(e) = entry(count, last) {
                    pairs.insert((*prev).to_string(), (*cur).to_string(), e);
                    loaded += 1;
                }
            }
            ["n", word, pinyin, at] if !word.is_empty() && !pinyin.is_empty() => {
                if let Ok(at_ms) = at.parse() {
                    let new = NewWord {
                        pinyin: (*pinyin).to_string(),
                        at_ms,
                    };
                    new_words.insert((*word).to_string(), new);
                    loaded += 1;
                }
            }
            _ => {}
        }
    }
    Ok(loaded)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lm::BigramLm;

    #[test]
    fn heat_decays_with_half_life() {
        let e = HistoryEntry {
            count: 4.0,
            last_used_ms: 0,
        };
        assert!((decayed(e, 0) - 4.0).abs() < 1e-9);
        assert!((decayed(e, HALF_LIFE_MS as u64) - 2.0).abs() < 1e-6);
    }

    #[test]
    fn bumped_keeps_fractions() {
        // 30 天前选过一次：衰减到一半再加 1，是 1.5 次，不取整成 2
        let once = bumped(None, 0);
        assert_eq!(once.count, 1.0);
        let again = bumped(Some(once), HALF_LIFE_MS as u64);
        assert!((again.count - 1.5).abs() < 1e-9);
    }

    #[test]
    fn journal_takes_ranges_newest_first() {
        let mut journal = Journal::default();
        let start = journal.mark();
        for i in 0..3 {
            journal.push(Change::LastWord {
                before: i.to_string(),
                after: (i + 1).to_string(),
            });
        }
        let end = journal.mark();
        assert_eq!(end - start, 3);
        let taken = journal.take(start + 1, end).unwrap();
        assert_eq!(taken.len(), 2);
        assert!(matches!(&taken[0], Change::LastWord { after, .. } if after == "3"));
        // 撤销过的不会再取出来
        assert_eq!(journal.take(start, end).unwrap().len(), 1);
        // 挤出日志的取不了
        for _ in 0..JOURNAL_MAX {
            journal.push(Change::LastWord {
                before: String::new(),
                after: String::new(),
            });
        }
        assert!(journal.take(start, end).is_none());
    }

    #[test]
    fn learned_pair_adds_to_score_and_uses_start_word() {
        let mut pairs = UserPairs::default();
        pairs.learn("发", "给", 1000);
        let base = BigramLm::default();
        let lm = LearnedLm {
            base: &base,
            pairs: &pairs,
            start: Some("发"),
            at_ms: 1000,
            mid_sentence: false,
        };
        assert!(lm.score(Some("发"), "给") > lm.score(Some("发"), "个"));
        // 句首没有前一个词时用 start
        assert!(lm.score(None, "给") > lm.score(None, "个"));
    }

    #[test]
    fn history_round_trips() {
        let dir = std::env::temp_dir().join(format!("wowtype-learn-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("history.tsv");
        let mut words = HashMap::new();
        words.insert(
            "种地".to_string(),
            HistoryEntry {
                count: 3.0,
                last_used_ms: 42,
            },
        );
        words.insert(
            "衰减过".to_string(),
            HistoryEntry {
                count: 1.2599210498948732,
                last_used_ms: 42,
            },
        );
        let mut pairs = UserPairs::default();
        pairs.learn("发", "给", 7);
        let mut new_words = HashMap::new();
        new_words.insert(
            "霜频阁".to_string(),
            NewWord {
                pinyin: "shuang pin ge".into(),
                at_ms: 40,
            },
        );
        save_history(&path, &words, &pairs, &new_words, 42).unwrap();
        let text = std::fs::read_to_string(&path).unwrap();
        assert!(text.contains("w\t衰减过\t1.26\t42\n"), "带小数的热度：{text}");
        assert!(text.contains("w\t种地\t3\t42\n"), "整数照旧：{text}");

        let mut w2 = HashMap::new();
        let mut p2 = UserPairs::default();
        let mut n2 = HashMap::new();
        assert_eq!(load_history(&path, &mut w2, &mut p2, &mut n2).unwrap(), 4);
        assert_eq!(w2["种地"].count, 3.0);
        assert_eq!(w2["衰减过"].count, 1.26);
        assert_eq!(p2.get("发", "给").unwrap().count, 1.0);
        assert_eq!(n2["霜频阁"].pinyin, "shuang pin ge");

        // 存盘时丢掉衰减到几乎没有的记录、过期的候选新词
        let later = 42 + 6 * HALF_LIFE_MS as u64;
        let text = history_text(&w2, &p2, &n2, later);
        assert!(!text.contains("衰减过"), "{text}");
        assert!(!text.contains("霜频阁"), "{text}");
        assert!(text.contains("种地"), "3 次的还在（6 个半衰期后约 0.05）：{text}");
        std::fs::remove_dir_all(&dir).ok();
    }
}
