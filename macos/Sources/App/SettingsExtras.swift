// 设置窗口里自成一块的几样：整句增强模型（导入 / 删除）、更新、「帮助」里的复制诊断信息和卸载。
// 前两样各自是单独的 ObservableObject，SettingsModel 只持有它们，界面在 SettingsView.swift 里。

import AppKit
import SwiftUI

// MARK: 帮助：诊断信息、卸载

extension SettingsModel {
    /// 收集诊断信息（Diagnostics，要几秒：读系统日志）复制到剪贴板，贴到问题里给开发者看
    func copyDiagnostics() {
        var errors: [String] = []
        if let releaseError { errors.append("查万象词库的发布页：\(releaseError)") }
        if let voiceError { errors.append("语音模型：\(voiceError)") }
        if let localError { errors.append("离线翻译模型：\(localError)") }
        if let error = nnlm.error { errors.append("整句增强模型：\(error)") }
        if let error = updater.error { errors.append("更新：\(error)") }
        let source = Installer.sourceSummary
        showToast("正在收集诊断信息…")
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let text = Diagnostics.collect(inputSource: source, errors: errors)
            DispatchQueue.main.async { [weak self] in
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(text, forType: .string)
                self?.showToast("诊断信息已复制，可以粘贴到问题里（不含打过的字）")
            }
        }
    }

    /// 「卸载 WowType」：确认后关掉输入法、去掉登记、删掉程序（Installer.uninstall），用户数据留着，然后退出设置窗口
    func confirmUninstall() {
        let alert = NSAlert()
        alert.messageText = "卸载 WowType？"
        let apps = Installer.installedApps().map { ($0.path as NSString).abbreviatingWithTildeInPath }
        let data = (supportDir.path as NSString).abbreviatingWithTildeInPath
        let list: String = apps.isEmpty ? " WowType.app" : "：\n" + apps.joined(separator: "\n")
        alert.informativeText = "会关掉 WowType 输入法、从系统里去掉，删掉\(list)\n\n"
            + "自定义词、学到的词和设置留在 \(data)，以后重新安装接着用；不要了可以自己删掉。"
        alert.addButton(withTitle: "卸载")
        alert.addButton(withTitle: "取消")
        if #available(macOS 11.0, *) {
            alert.buttons.first?.hasDestructiveAction = true
        }
        let answered: (NSApplication.ModalResponse) -> Void = { [weak self] response in
            guard response == .alertFirstButtonReturn, let self else { return }
            self.save()
            let result = Installer.uninstall(supportDir: self.supportDir)
            let done = NSAlert()
            done.messageText = result.ok ? "已卸载" : "没卸载干净"
            done.informativeText = result.message
            done.addButton(withTitle: result.ok ? "退出" : "好")
            done.runModal()
            if result.ok {
                NSApp.terminate(nil)
            }
        }
        if let window = NSApp.keyWindow ?? NSApp.windows.first(where: \.isVisible) {
            alert.beginSheetModal(for: window, completionHandler: answered)
        } else {
            answered(alert.runModal())
        }
    }
}

/// 「输入」页的「整句增强模型」：从微信输入法导入 nlrgpt2_nnlm.bin（和 Linux 设置的一样）。规矩在 NnlmInstaller。
final class NnlmModel: ObservableObject {
    @Published private(set) var busy = false
    /// 上次没导入成功的原因，再导入时清掉
    @Published private(set) var error: String?
    /// 导入、删除后加一，让界面重新看文件
    @Published private var revision = 0
    let supportDir: URL
    /// 安装包里的模型（WowType 以后随包附带；现在没有是 nil）
    let bundled: String?
    /// 提示条（SettingsModel 设成它的 showToast）
    var toast: (String) -> Void = { _ in }

    init(supportDir: URL, bundled: String?) {
        self.supportDir = supportDir
        self.bundled = bundled
    }

    private var importedPath: String { NnlmInstaller.model(in: supportDir).path }

    var imported: Bool {
        _ = revision
        return FileManager.default.fileExists(atPath: importedPath)
    }

    var status: String {
        _ = revision
        if busy { return "正在导入…" }
        if imported {
            return "已导入微信输入法的模型（\(LocalModel.sizeText(LocalModelInstaller.fileSize(importedPath)))）"
        }
        if let error { return "没导入：\(error)" }
        if bundled != nil { return "用安装包里的模型" }
        return "没有：从微信输入法导入后整句候选更准"
    }

    /// 选微信输入法的安装包（.zip）、WeType.app 或模型文件本身
    func choose() {
        guard !busy else { return }
        let panel = NSOpenPanel()
        panel.title = "导入整句增强模型"
        panel.message = "选微信输入法 macOS 版的安装包（.zip）、装好的 WeType.app，或者 \(NnlmInstaller.fileName)"
        panel.prompt = "导入"
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.directoryURL = URL(fileURLWithPath: "/Applications", isDirectory: true)
        let done: (NSApplication.ModalResponse) -> Void = { [weak self] response in
            guard response == .OK, let url = panel.url else { return }
            self?.importModel(from: url)
        }
        if let window = NSApp.keyWindow ?? NSApp.windows.first(where: \.isVisible) {
            panel.beginSheetModal(for: window, completionHandler: done)
        } else {
            done(panel.runModal())
        }
    }

    func importModel(from url: URL) {
        guard !busy else { return }
        busy = true
        error = nil
        let supportDir = self.supportDir
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            // zip 有几百 MB 时解压要一会儿，放后台
            let result = Result { try NnlmInstaller.install(from: url, supportDir: supportDir) }
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.busy = false
                self.revision += 1
                switch result {
                case .success:
                    // 输入法下一次按键时看到文件变了，在后台重新载入
                    self.toast("已导入整句增强模型，几秒后生效")
                case .failure(let error as PackError):
                    self.error = error.description
                case .failure(let error):
                    self.error = error.localizedDescription
                }
            }
        }
    }

    /// 删掉导入的模型（安装包里的不动）；输入法看到文件没了就重新载入
    func remove() {
        guard !busy else { return }
        let freed = NnlmInstaller.remove(supportDir: supportDir)
        error = nil
        revision += 1
        toast(freed > 0 ? "已删除整句增强模型，空出 \(LocalModel.sizeText(freed))" : "已删除")
    }
}

/// 「常规」页的「更新」：打开设置窗口时（CheckUpdate 开着）查一次 GitHub，有新版给「下载并安装」。
/// 下载、校验、交给 installer 在 UpdateInstaller 里；装的时候设置窗口会被 pkg 的 preinstall 停掉，装完重新打开时
/// init 里读到结果显示出来。
final class UpdateModel: ObservableObject {
    @Published private(set) var checking = false
    @Published private(set) var release: LexiconPackInstaller.Release?
    /// 查、下载、装失败的原因（有它时给「打开发布页」）
    @Published private(set) var error: String?
    /// 下载中（已下, 总共）；没在下是 nil
    @Published private(set) var downloadProgress: (Int64, Int64)?
    @Published private(set) var installing = false
    /// 装好了：给用户看的一句话
    @Published private(set) var installed: String?
    let supportDir: URL
    /// 交给 installer 之前调（SettingsModel 设成 save：设置窗口会被停掉，没写的设置先写）
    var beforeInstall: () -> Void = {}
    private var control: DownloadControl?
    private var watch: Timer?

    init(supportDir: URL) {
        self.supportDir = supportDir
        // 上一个设置窗口点了「下载并安装」、被 installer 停掉了：这是装完重新打开的
        if let result = UpdateInstaller.takeResult(supportDir: supportDir) {
            show(result, restarted: true)
        }
    }

    /// 发布页上最新正式版的版本号（查过才有）
    var latestVersion: String? {
        release.flatMap { AppUpdate.version(tag: $0.tag) }
    }

    var hasUpdate: Bool {
        latestVersion.map { AppUpdate.isNewer($0, than: appVersion) } ?? false
    }

    var busy: Bool { downloadProgress != nil || installing }

    var title: String {
        if hasUpdate, let latest = latestVersion { return "有新版本 \(latest)" }
        return "当前版本 \(appVersion)"
    }

    var subtitle: String? {
        if installing { return "正在安装…设置窗口会关掉，装好后自动重新打开" }
        if let (done, total) = downloadProgress {
            if done == 0 { return "正在连接…" }
            return total > 0
                ? "正在下载：\(LocalModel.sizeText(done)) / \(LocalModel.sizeText(total))（\(LocalModel.percent(done, of: total))%）"
                : "正在下载：\(LocalModel.sizeText(done))"
        }
        if let error { return error }
        if let installed { return installed }
        if checking { return "正在检查更新…" }
        guard let latest = latestVersion else { return nil }
        if hasUpdate { return "现在是 \(appVersion)，下载后自动安装，不要密码" }
        if !AppUpdate.isRelease(appVersion) { return "最新正式版 \(latest)，现在装的是开发版" }
        return "已经是最新版本"
    }

    func check() {
        guard !checking, !busy else { return }
        checking = true
        error = nil
        let supportDir = self.supportDir
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let result = Result { try UpdateInstaller.fetchLatest(supportDir: supportDir) }
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.checking = false
                switch result {
                case .success(let release):
                    self.release = release
                case .failure(let error as PackError):
                    self.error = "检查更新失败：\(error.description)"
                case .failure(let error):
                    self.error = "检查更新失败：\(error.localizedDescription)"
                }
            }
        }
    }

    func downloadAndInstall() {
        guard let release, hasUpdate, !busy else { return }
        let control = DownloadControl()
        self.control = control
        error = nil
        installed = nil
        downloadProgress = (0, 0)
        let supportDir = self.supportDir
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let result = Result {
                try UpdateInstaller.download(release, supportDir: supportDir, control: control) { done, total in
                    DispatchQueue.main.async { [weak self] in
                        guard let self, self.control === control else { return }
                        self.downloadProgress = (done, total)
                    }
                }
            }
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.control = nil
                self.downloadProgress = nil
                switch result {
                case .success(let pkg):
                    self.install(pkg)
                case .failure(let error as DownloadCancelled):
                    self.error = error.description
                case .failure(let error as PackError):
                    self.error = "下载失败：\(error.description)"
                case .failure(let error):
                    self.error = "下载失败：\(error.localizedDescription)"
                }
            }
        }
    }

    func cancelDownload() {
        control?.cancel()
    }

    /// 出错时给的链接：这个版本的发布页（还没查到是总的发布页）
    func openReleasePage() {
        NSWorkspace.shared.open(release.map { UpdateInstaller.releasePage(tag: $0.tag) } ?? UpdateInstaller.releasesPage)
    }

    private func install(_ pkg: URL) {
        beforeInstall()
        do {
            try UpdateInstaller.launchInstaller(pkg: pkg, supportDir: supportDir)
        } catch {
            self.error = "安装没能开始：\(error.localizedDescription)"
            return
        }
        installing = true
        // 一般这个窗口会被 preinstall 停掉；没被停掉（比如 installer 一开始就失败了）时自己等结果
        watch?.invalidate()
        let timer = Timer(timeInterval: 1, repeats: true) { [weak self] timer in
            guard let self else {
                timer.invalidate()
                return
            }
            guard let result = UpdateInstaller.takeResult(supportDir: self.supportDir) else { return }
            timer.invalidate()
            self.watch = nil
            self.installing = false
            self.show(result, restarted: false)
        }
        RunLoop.main.add(timer, forMode: .common)
        watch = timer
    }

    private func show(_ result: (ok: Bool, detail: String), restarted: Bool) {
        if result.ok {
            installed = restarted ? "已更新到 \(appVersion)" : "新版本装好了，重新打开设置窗口后生效"
        } else {
            error = "安装失败（\(result.detail)），可以到发布页下载安装包自己装"
        }
    }
}
