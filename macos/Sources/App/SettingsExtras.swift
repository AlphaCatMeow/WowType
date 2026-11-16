// 设置窗口里自成一块的几样：整句增强模型（导入 / 删除）。
// 各自是单独的 ObservableObject，SettingsModel 只持有它们，界面在 SettingsView.swift 里。

import AppKit
import SwiftUI

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
