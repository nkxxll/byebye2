./Screenshot 2026-09-19 at 20.32.50.png shows a tui app I wrote I want to make a platform independent native gui app out of it with your help. the app does the following:

1. opens as a float in the middle of the screen; can be started by either cli or with a keymap or with a launcher
2. lets you select whether you want to suspend, lock, logout, hibernate, restart, shutdown, sleep (if things are not an option for the current platform win, mac, linux then just skip it
3. the selection should be possible with shortcut keymap like s for suspend and sensible others for the rest or by number in the list or with a mouse click
4. the buttons should be perform the platform native way of doing the action like shuting down or suspending etc.
5. if the action fails show the error in a error dialog
6. if it succeeds close the application

For all technical decision chose the best with the lowest number of dependencies.
Everything else I trust your judgement...
