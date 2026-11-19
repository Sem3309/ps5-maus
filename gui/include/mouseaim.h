// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// "GTA mode": play with mouse + keyboard. Mouse movement -> right stick,
// WASD -> left stick, mouse buttons / keys -> controller buttons.

#ifndef CHIAKI_MOUSEAIM_H
#define CHIAKI_MOUSEAIM_H

#include "mouseaimcurve.h"

#include <chiaki/controller.h>

#include <QObject>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QAbstractNativeEventFilter>
#include <QPointer>

class QWindow;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

class MouseAim : public QObject, public QAbstractNativeEventFilter
{
	Q_OBJECT

	public:
		explicit MouseAim(QWindow *window, QObject *parent = nullptr);
		~MouseAim() override;

		// Whether the user has GTA mode switched on (F8 toggles it)
		bool IsEnabled() const { return enabled; }
		// Enabled and allowed to take input right now (stream visible, no menu open, window focused)
		bool IsActive() const { return enabled && session_running && !suspended && window_active; }

		void SetSessionRunning(bool running);
		void SetSuspended(bool suspended);   // menu / overlay open
		void Toggle();

		// Event entry points. Return true when the event was consumed.
		bool HandleKey(QKeyEvent *event);
		bool HandleMousePress(QMouseEvent *event);
		bool HandleMouseRelease(QMouseEvent *event);
		bool HandleMouseMove(QMouseEvent *event);
		bool HandleWheel(QWheelEvent *event);

		ChiakiControllerState GetState() const { return state; }
		QString ConfigPath() const { return config_path; }

		bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

	signals:
		void StateChanged();
		void StatusMessage(const QString &message);

	private:
		enum Target : int
		{
			// values >= 0 are ChiakiControllerButton bits
			T_L2 = -1,
			T_R2 = -2,
			T_WALK = -3,
			T_MOVE_UP = -4,
			T_MOVE_DOWN = -5,
			T_MOVE_LEFT = -6,
			T_MOVE_RIGHT = -7,
		};
		// pseudo key codes for mouse buttons and wheel in the binding table
		static constexpr int MOUSE_LEFT = 0x7f000001;
		static constexpr int MOUSE_RIGHT = 0x7f000002;
		static constexpr int MOUSE_MIDDLE = 0x7f000003;
		static constexpr int MOUSE_X1 = 0x7f000004;
		static constexpr int MOUSE_X2 = 0x7f000005;
		static constexpr int WHEEL_UP = 0x7f000006;
		static constexpr int WHEEL_DOWN = 0x7f000007;

		void LoadConfig();
		void WriteDefaultConfig();
		void SaveSensitivity();
		void UpdateActivation();
		void LockCursor(bool lock);
		void RecenterCursor();
		void Tick();
		void InputDown(int code);
		void InputUp(int code);
		void ApplyTarget(int target, bool down);
		void RebuildState();
		static int ParseInput(const QString &name);
		static int ParseTarget(const QString &name);
		void AddMovement(double dx, double dy);

		QPointer<QWindow> window;
		QString config_path;

		bool enabled = false;
		bool session_running = false;
		bool suspended = false;
		bool window_active = false;
		bool cursor_locked = false;
		bool raw_input_registered = false;
		bool raw_input_seen = false;

		MouseAimParams params;
		MouseAimCurve curve;
		QElapsedTimer clock;
		QTimer tick_timer;
		double walk_factor = 0.45;
		int wheel_pulse_ms = 80;

		QHash<int, QList<int>> bindings;   // input code -> targets
		QSet<int> held_inputs;
		QHash<int, int> target_count;      // target -> number of inputs holding it
		QHash<int, int> wheel_release_at;  // input code -> pending release

		ChiakiControllerState state;
		StickOut right_stick;
};

#endif // CHIAKI_MOUSEAIM_H
