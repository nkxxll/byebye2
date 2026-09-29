import Foundation
import Carbon
import ByebyeCore

let visibleActions: [Action] = [LOCK, LOGOUT, SLEEP, SHUTDOWN, RESTART]

func actionInfo(_ action: Action) -> ActionInfo {
    withUnsafePointer(to: actions) {
        UnsafeRawPointer($0).assumingMemoryBound(to: ActionInfo.self)[Int(action.rawValue)]
    }
}

func actionLabel(_ action: Action) -> String { String(cString: actionInfo(action).label) }
func actionKey(_ action: Action) -> String { String(UnicodeScalar(UInt8(actionInfo(action).key))) }

// Ask loginwindow for the same graceful operations as the Apple menu.
private func systemEvent(_ eventID: AEEventID) -> String? {
    var psn = ProcessSerialNumber(highLongOfPSN: 0, lowLongOfPSN: UInt32(kSystemProcess))
    var target = AEAddressDesc()
    var event = AppleEvent()
    var reply = AppleEvent()
    defer {
        AEDisposeDesc(&reply)
        AEDisposeDesc(&event)
        AEDisposeDesc(&target)
    }
    var status = OSStatus(AECreateDesc(DescType(typeProcessSerialNumber), &psn, MemoryLayout.size(ofValue: psn), &target))
    if status == noErr {
        status = OSStatus(AECreateAppleEvent(AEEventClass(kCoreEventClass), eventID, &target,
                                   AEReturnID(kAutoGenerateReturnID), AETransactionID(kAnyTransactionID), &event))
    }
    if status == noErr {
        status = AESendMessage(&event, &reply, AESendMode(kAEWaitReply | kAECanInteract), kAEDefaultTimeout)
    }
    if status == noErr {
        var replyError: Int32 = 0
        if AEGetParamPtr(&reply, AEKeyword(keyErrorNumber), DescType(typeSInt32), nil,
                         &replyError, MemoryLayout.size(ofValue: replyError), nil) == noErr {
            status = replyError
        }
    }
    return status == noErr ? nil : "macOS refused the request (OSStatus \(status)). The operation may have been cancelled or blocked by another application."
}

func performAction(_ action: Action, options: Options) -> String? {
    var error = [CChar](repeating: 0, count: 512)
    if mock_action(options, action, &error, error.count) {
        return error[0] == 0 ? nil : String(cString: error)
    }
    switch action {
    case SLEEP: return systemEvent(AEEventID(kAESleep))
    case LOGOUT: return systemEvent(AEEventID(kAEReallyLogOut))
    case SHUTDOWN: return systemEvent(AEEventID(kAEShutDown))
    case RESTART: return systemEvent(AEEventID(kAERestart))
    case LOCK:
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
        task.arguments = ["-e", "tell application \"System Events\" to key code 12 using {control down, command down}"]
        let pipe = Pipe()
        task.standardError = pipe
        do { try task.run() } catch { return error.localizedDescription }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        task.waitUntilExit()
        guard task.terminationStatus != 0 else { return nil }
        let detail = String(data: data, encoding: .utf8) ?? "Lock Screen could not be activated."
        return "\(detail)\nAllow Byebye (or your terminal when launched from CLI) in System Settings → Privacy & Security → Accessibility and Automation, then retry."
    default: return "This action is not available on macOS."
    }
}
