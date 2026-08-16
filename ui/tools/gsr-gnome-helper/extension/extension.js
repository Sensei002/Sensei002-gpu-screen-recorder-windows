import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';

const DAEMON_NAME = 'com.dec05eba.gpu_screen_recorder';
const DAEMON_PATH = '/';
const DAEMON_INTERFACE = 'com.dec05eba.gpu_screen_recorder';

export default class GsrFocusedWindowExtension extends Extension {
    enable() {
        this._display = global.display;
        this._currentWindow = null;
        this._windowSignals = [];
        this._lastTitle = null;
        this._lastFullscreen = null;
        this._lastMonitor = null;

        this._focusSignalId = this._display.connect('notify::focus-window', () => this._onFocusChanged());
        this._onFocusChanged();

        // Whenever the helper's bus name appears (e.g. gsr-ui starts), forget
        // the cached state and re-emit so the helper gets the current title
        // immediately rather than only after the next focus/title change.
        this._helperWatchId = Gio.bus_watch_name(
            Gio.BusType.SESSION,
            DAEMON_NAME,
            Gio.BusNameWatcherFlags.NONE,
            () => {
                this._lastTitle = null;
                this._lastFullscreen = null;
                this._lastMonitor = null;
                this._emitUpdate();
            },
            null
        );
    }

    disable() {
        if (this._helperWatchId) {
            Gio.bus_unwatch_name(this._helperWatchId);
            this._helperWatchId = 0;
        }
        if (this._focusSignalId) {
            try { this._display.disconnect(this._focusSignalId); } catch (e) {}
            this._focusSignalId = 0;
        }
        this._unsubscribeWindow();
        this._currentWindow = null;
        this._lastTitle = null;
        this._lastFullscreen = null;
        this._lastMonitor = null;
        this._display = null;
    }

    _unsubscribeWindow() {
        if (this._currentWindow) {
            for (const id of this._windowSignals) {
                try { this._currentWindow.disconnect(id); } catch (e) {}
            }
        }
        this._windowSignals = [];
    }

    _onFocusChanged() {
        const window = this._display ? this._display.focus_window : null;

        if (window !== this._currentWindow) {
            this._unsubscribeWindow();
            this._currentWindow = window;
            if (window) {
                this._windowSignals.push(window.connect('notify::title', () => this._emitUpdate()));
                this._windowSignals.push(window.connect('notify::fullscreen', () => this._emitUpdate()));
                this._windowSignals.push(window.connect('position-changed', () => this._emitUpdate()));
            }
        }
        this._emitUpdate();
    }

    _shouldIgnoreWindow(window) {
        if (!window) return false;
        const wmClass = window.get_wm_class() || '';
        const wmInst = window.get_wm_class_instance() || '';
        if (wmClass === 'gsr-ui' || wmInst === 'gsr-ui') return true;
        if (wmClass === 'gsr-notify' || wmInst === 'gsr-notify') return true;
        return false;
    }

    _emitUpdate() {
        const window = this._currentWindow;

        let title = '';
        let fullscreen = false;
        let monitor = '';

        if (window && !this._shouldIgnoreWindow(window) && window.window_type === Meta.WindowType.NORMAL) {
            title = window.get_title() || '';
            fullscreen = window.is_fullscreen();
        }

        if (title === this._lastTitle && fullscreen === this._lastFullscreen && monitor === this._lastMonitor)
            return;

        this._lastTitle = title;
        this._lastFullscreen = fullscreen;
        this._lastMonitor = monitor;

        try {
            Gio.DBus.session.call(
                DAEMON_NAME, DAEMON_PATH, DAEMON_INTERFACE,
                'updateActiveWindow',
                new GLib.Variant('(sbs)', [title, fullscreen, monitor]),
                null,
                Gio.DBusCallFlags.NO_AUTO_START,
                500,
                null,
                null
            );
        } catch (e) {
            // Helper may not be running yet — best-effort.
        }
    }
}
