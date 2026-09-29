import SwiftUI
import ByebyeCore

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

@main
struct ByebyeApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var model: MenuModel

    init() {
        var options = Options()
        guard parse_options(CommandLine.argc, CommandLine.unsafeArgv, &options) else {
            print_help()
            exit(2)
        }
        if options.help { print_help(); exit(0) }
        if options.list {
            for (index, action) in visibleActions.enumerated() {
                print("\(index + 1)  \(actionLabel(action)) [\(actionKey(action))]")
            }
            exit(0)
        }
        _model = StateObject(wrappedValue: MenuModel(options: options))
    }

    var body: some Scene {
        WindowGroup { MenuView(model: model) }
            .windowStyle(.hiddenTitleBar)
            .commands { CommandGroup(replacing: .newItem) { } }
    }
}
