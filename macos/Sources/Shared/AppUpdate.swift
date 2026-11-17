// 检查更新的规矩（macOS）：发布页的 tag 怎么变成版本号、macOS 安装包叫什么、什么时候算有新版、多久查一次。
// 包名和 install.sh 的 macOS 分支一样（WowType-<版本>-macos.pkg，arm64 + x86_64 通用），版本号的写法也照它认；
// 比版本用 LexiconPack.compareVersions（按数字段逐段比，和 Linux 设置的 parse_version 一样）。
// 下载、校验、交给系统的 installer 在 App 层（UpdateInstaller），这里不依赖 Foundation，测试在 Linux 上也能跑。

public enum AppUpdate {
    /// 发布页（出错时给用户的链接）
    public static let releasesPage = "https://github.com/Gloridust/WowType/releases"
    /// 输入法进程最多隔这么久查一次（秒）：一天
    public static let checkInterval: Int64 = 24 * 3600

    /// tag（v0.4.1 或 0.4.1）里的版本号；不像版本号的（空的、带 / 空格的）是 nil。和 install.sh 一样：
    /// 字母或数字开头，后面只有字母、数字和 . _ + ~ ^ -
    public static func version(tag: String) -> String? {
        var v = Substring(tag)
        if v.hasPrefix("v") { v = v.dropFirst() }
        guard let first = v.unicodeScalars.first, isAlnum(first),
              v.unicodeScalars.allSatisfy({ isAlnum($0) || "._+~^-".unicodeScalars.contains($0) })
        else { return nil }
        return String(v)
    }

    /// 发布页上的 macOS 安装包
    public static func packageName(version: String) -> String {
        "WowType-\(version)-macos.pkg"
    }

    /// 正式版的版本号（数字开头，0.4.0 这样）；开发时编出来的是 dev
    public static func isRelease(_ version: String) -> Bool {
        guard let first = version.unicodeScalars.first else { return false }
        return ("0"..."9").contains(first)
    }

    /// GitHub 发布列表里的一项（Beta 计划挑版本用）
    public struct Listed: Equatable {
        public let tag: String
        public let prerelease: Bool
        public let draft: Bool

        public init(tag: String, prerelease: Bool = false, draft: Bool = false) {
            self.tag = tag
            self.prerelease = prerelease
            self.draft = draft
        }
    }

    /// Beta 计划：发布列表里版本号最大的那个的下标，预发布也算，一样大时要正式版；没有合适的是 nil。
    /// 只看 v 加数字开头的 tag：数据的发布（lm-…、nnlm-…）也是预发布，不是新版本。草稿不算。和 Linux 的 pick_release 一样
    public static func pickNewest(_ releases: [Listed]) -> Int? {
        var best: Int?
        for (i, r) in releases.enumerated() where !r.draft && isAppTag(r.tag) {
            guard let b = best else {
                best = i
                continue
            }
            let c = LexiconPack.compareVersions(r.tag, releases[b].tag)
            if c > 0 || (c == 0 && releases[b].prerelease && !r.prerelease) {
                best = i
            }
        }
        return best
    }

    /// v0.5.0 这样的 tag（v 加数字和点）
    static func isAppTag(_ tag: String) -> Bool {
        guard tag.hasPrefix("v") || tag.hasPrefix("V") else { return false }
        let rest = tag.unicodeScalars.dropFirst()
        guard let first = rest.first, ("0"..."9").contains(first), rest.last != "." else { return false }
        return rest.allSatisfy { ("0"..."9").contains($0) || $0 == "." }
    }

    /// latest 比现在装的 current 新。current 不是正式版（dev）时不算：开发版不提示更新（和 Linux 一样）
    public static func isNewer(_ latest: String, than current: String) -> Bool {
        isRelease(current) && isRelease(latest) && LexiconPack.compareVersions(latest, current) > 0
    }

    /// 数据目录里 update-state 记的：上次查成功的时间（Unix 秒）、那时发布页上最新的 tag。
    /// 输入法和设置窗口都读写它：一边查过了另一边不用再查，输入法菜单也知道有没有新版。
    public struct State: Equatable {
        public var checked: Int64
        public var latest: String

        public init(checked: Int64 = 0, latest: String = "") {
            self.checked = checked
            self.latest = latest
        }

        /// 一行一项「键=值」；不认识的行、写坏的值不管
        public init(text: String) {
            self.init()
            for line in text.split(whereSeparator: { $0 == "\n" || $0 == "\r\n" || $0 == "\r" }) {
                guard let eq = line.firstIndex(of: "=") else { continue }
                let key = line[..<eq]
                let value = String(line[line.index(after: eq)...])
                switch key {
                case "checked": checked = Int64(value) ?? checked
                case "latest": latest = AppUpdate.version(tag: value) == nil ? latest : value
                default: break
                }
            }
        }

        public var text: String {
            "checked=\(checked)\nlatest=\(latest)\n"
        }
    }

    /// 该查了：上次查过一天了（时钟往回拨了也算）
    public static func due(_ state: State, now: Int64) -> Bool {
        now - state.checked >= checkInterval || now < state.checked
    }

    private static func isAlnum(_ s: Unicode.Scalar) -> Bool {
        ("0"..."9").contains(s) || ("a"..."z").contains(s) || ("A"..."Z").contains(s)
    }
}
