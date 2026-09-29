import SwiftUI
import AppKit
import ByebyeCore

@MainActor
final class MenuModel: ObservableObject {
    @Published var selected = 0
    @Published private(set) var busy = false
    @Published var showingError = false
    @Published private(set) var errorMessage = ""
    let options: Options
    private let terminate: @MainActor () -> Void

    init(options: Options, terminate: @escaping @MainActor () -> Void = { NSApp.terminate(nil) }) {
        self.options = options
        self.terminate = terminate
    }

    var footer: String {
        if busy { return "Waiting for macOS…" }
        if options.simulate_error { return "TEST MODE · selections show a simulated error" }
        if options.dry_run { return "DRY RUN · selections close without taking action" }
        return "↑ ↓ ← → / Tab navigate · Enter select · Esc / q quit"
    }

    func activate(_ index: Int) {
        guard !busy, !showingError, visibleActions.indices.contains(index) else { return }
        selected = index
        busy = true
        let action = visibleActions[index]
        let options = options
        Task {
            let error = await Task.detached(priority: .userInitiated) {
                performAction(action, options: options)
            }.value
            if let error {
                errorMessage = error
                showingError = true
                busy = false
            } else {
                terminate()
            }
        }
    }

    func handleKey(_ key: UInt32, modifiers: NSEvent.ModifierFlags = [], repeatKey: Bool = false) -> Bool {
        guard !busy, !showingError, modifiers.intersection([.command, .control, .option]).isEmpty else { return false }
        if repeatKey { return true }
        switch key {
        case 27, 113, 81: terminate()
        case 0xF700, 0xF702, 0xF701, 0xF703, 9, 25:
            let backward = key == 0xF700 || key == 0xF702 || key == 25 || (key == 9 && modifiers.contains(.shift))
            selected = Int(move_selection(Int32(selected), Int32(visibleActions.count), backward ? -1 : 1))
        case 13, 10, 32: activate(selected)
        default:
            let index = action_for_key(visibleActions, Int32(visibleActions.count), key)
            guard index >= 0 else { return false }
            activate(Int(index))
        }
        return true
    }
}

private let menuBackground = Color(red: 0.115, green: 0.125, blue: 0.125)
private let muted = Color(red: 0.66, green: 0.63, blue: 0.57)
private let accent = Color(red: 0.48, green: 0.72, blue: 0.48)

struct MenuView: View {
    @ObservedObject var model: MenuModel
    @FocusState private var focused: Int?

    private func actionButton(_ index: Int) -> some View {
        let action = visibleActions[index]
        return Button { model.activate(index) } label: {
            Text("\(index + 1)  \(actionLabel(action))   [\(actionKey(action))]")
                .font(.system(size: 19, weight: .medium, design: .monospaced))
                .foregroundColor(model.selected == index ? accent : muted)
                .frame(width: 240, height: 58)
                .contentShape(RoundedRectangle(cornerRadius: 7))
        }
        .buttonStyle(MenuButtonStyle(selected: model.selected == index))
        .focused($focused, equals: index)
        .accessibilityLabel("\(actionLabel(action)). Shortcut \(actionKey(action)) or \(index + 1)")
        .disabled(model.busy || model.showingError)
    }

    var body: some View {
        VStack(spacing: 0) {
            Text("Where do you want to GO?")
                .font(.system(size: 24, weight: .semibold, design: .monospaced))
                .foregroundColor(Color(red: 0.3, green: 0.64, blue: 0.66))
                .padding(.top, 59)
                .padding(.bottom, 33)
            VStack(spacing: 20) {
                HStack(spacing: 20) { actionButton(0); actionButton(1) }
                HStack(spacing: 20) { actionButton(2); actionButton(3) }
                actionButton(4)
            }
            Spacer(minLength: 0)
            Text(model.footer).font(.system(size: 12)).foregroundColor(muted).padding(.bottom, 27)
        }
        .frame(width: 580, height: 410)
        .background(menuBackground)
        .background(WindowBridge(model: model))
        .onAppear { focused = model.selected }
        .onChange(of: model.selected) { focused = $0 }
        .onChange(of: focused) { if let index = $0 { model.selected = index } }
        .onChange(of: model.showingError) { if !$0 { focused = model.selected } }
        .alert("\(actionLabel(visibleActions[model.selected])) failed", isPresented: $model.showingError) {
            Button("OK", role: .cancel) { }
        } message: { Text(model.errorMessage) }
    }
}

private struct MenuButtonStyle: ButtonStyle {
    let selected: Bool
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(configuration.isPressed ? accent.opacity(0.12) : menuBackground)
            .clipShape(RoundedRectangle(cornerRadius: 7))
            .overlay(RoundedRectangle(cornerRadius: 7).strokeBorder(selected ? accent : muted, lineWidth: selected ? 2 : 1))
    }
}

// SwiftUI on macOS 12 has no floating-window or arbitrary key-event modifiers.
private struct WindowBridge: NSViewRepresentable {
    let model: MenuModel
    func makeNSView(context: Context) -> WindowView { WindowView(model: model) }
    func updateNSView(_ nsView: WindowView, context: Context) { }
}

private final class WindowView: NSView {
    let model: MenuModel
    private var monitor: Any?
    init(model: MenuModel) { self.model = model; super.init(frame: .zero) }
    required init?(coder: NSCoder) { fatalError("init(coder:) is unsupported") }
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        if let monitor { NSEvent.removeMonitor(monitor); self.monitor = nil }
        guard let window else { return }
        window.title = "Byebye"
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.styleMask.insert(.fullSizeContentView)
        window.styleMask.remove(.resizable)
        window.backgroundColor = NSColor(red: 0.115, green: 0.125, blue: 0.125, alpha: 1)
        window.level = .floating
        window.collectionBehavior = [.moveToActiveSpace, .fullScreenAuxiliary]
        window.isMovableByWindowBackground = true
        window.center()
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self, weak window] event in
            guard let self, let window, event.window === window, window.attachedSheet == nil,
                  let key = event.charactersIgnoringModifiers?.unicodeScalars.first else { return event }
            return self.model.handleKey(key.value, modifiers: event.modifierFlags, repeatKey: event.isARepeat) ? nil : event
        }
    }
    deinit { if let monitor { NSEvent.removeMonitor(monitor) } }
}
