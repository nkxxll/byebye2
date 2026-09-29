import SwiftUI
import AppKit
import ByebyeCore

@main
struct MacMenuTests {
    @MainActor
    static func main() {
        NSApplication.shared.setActivationPolicy(.accessory)
        Task { await runChecks() }
        NSApp.run()
    }

    @MainActor
    static func runChecks() async {
        var options = Options()
        options.dry_run = true
        var terminated = false
        func check(_ value: @autoclosure () -> Bool, _ message: String) {
            guard value() else { fatalError(message) }
        }
        func waitFor(_ condition: () -> Bool) async {
            for _ in 0..<250 {
                if condition() { return }
                try? await Task.sleep(nanoseconds: 20_000_000)
            }
            check(condition(), "Timed out")
        }
        let model = MenuModel(options: options, terminate: { terminated = true })
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 580, height: 410),
                              styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.contentView = NSHostingView(rootView: MenuView(model: model))
        window.makeKeyAndOrderFront(nil)
        window.contentView?.layoutSubtreeIfNeeded()
        await waitFor { window.isVisible && window.level == .floating }
        check(window.level == .floating, "Window must float")
        let tab = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
                                  windowNumber: window.windowNumber, context: nil, characters: "\t",
                                  charactersIgnoringModifiers: "\t", isARepeat: false, keyCode: 48)!
        NSApp.sendEvent(tab)
        check(model.selected == 1, "Window event monitor routes Tab")
        model.selected = 0
        check(model.handleKey(9) && model.selected == 1, "Tab advances")
        check(model.handleKey(9, modifiers: .shift) && model.selected == 0, "Shift Tab reverses")
        check(model.handleKey(0xF702) && model.selected == 4, "Left wraps")
        check(model.handleKey(0xF703) && model.selected == 0, "Right wraps")
        check(!model.handleKey(120), "Unknown key passes through")
        check(!model.handleKey(113, modifiers: .command) && !terminated, "Command keys pass through")
        check(model.handleKey(9, repeatKey: true) && model.selected == 0, "Repeats suppressed")
        try? await Task.sleep(nanoseconds: 200_000_000)
        if CommandLine.arguments.count > 1, let view = window.contentView {
            view.layoutSubtreeIfNeeded()
            let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds)!
            view.cacheDisplay(in: view.bounds, to: bitmap)
            try! bitmap.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
        }
        for (index, action) in visibleActions.enumerated() {
            for key in [UInt32(49 + index), actionKey(action).unicodeScalars.first!.value] {
                terminated = false
                let item = MenuModel(options: options, terminate: { terminated = true })
                check(item.handleKey(key), "Action shortcut handled")
                check(item.busy, "Busy immediately")
                check(!item.handleKey(49), "Duplicate requests blocked")
                await waitFor { terminated }
                check(item.selected == index, "Correct action selected")
            }
        }
        for key: UInt32 in [27, 113, 81] {
            terminated = false
            let quitting = MenuModel(options: options, terminate: { terminated = true })
            check(quitting.handleKey(key) && terminated, "Quit shortcuts terminate")
        }
        options.simulate_error = true
        let failing = MenuModel(options: options, terminate: { fatalError("Failure must not terminate") })
        window.contentView = NSHostingView(rootView: MenuView(model: failing))
        failing.activate(3)
        await waitFor { failing.showingError }
        check(!failing.busy && failing.errorMessage.contains("Simulated Shutdown failure"), "Error retained")
        check(!failing.handleKey(49), "Shortcuts blocked during alert")
        // Dismiss through the same SwiftUI binding and retry the button's action.
        failing.showingError = false
        failing.activate(0)
        await waitFor { failing.showingError }
        check(failing.errorMessage.contains("Simulated Lock failure"), "Retry works")
        window.orderOut(nil)
        print("macOS SwiftUI checks passed. No system actions performed.")
        exit(0)
    }
}
