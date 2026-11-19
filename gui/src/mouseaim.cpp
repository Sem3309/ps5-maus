// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "mouseaim.h"

#include <QWindow>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QCursor>
#include <QScreen>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QStandardPaths>
#include <QKeySequence>
#include <QRegularExpression>
#include <QDebug>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#endif

static const char *DEFAULT_CONFIG = R"INI(; ============================================================
;  GTA-Modus (Maus + Tastatur) fuer chiaki-ng
;  F8 = GTA-Modus an/aus    F6 / F7 = Empfindlichkeit - / +
;  Aenderungen hier werden beim naechsten Einschalten (F8) geladen.
; ============================================================

[allgemein]
; GTA-Modus automatisch einschalten, sobald der Stream laeuft (1 = ja, 0 = nein)
aktiv_beim_start=1
; Geschwindigkeit beim Gehen (Taste "Walk"), 0.1 - 1.0
gehen_faktor=0.45

[maus]
; Kamera-Geschwindigkeit. Hoeher = schneller.
empfindlichkeit=1.0
; Vertikal im Verhaeltnis zu horizontal (0.5 = halb so schnell hoch/runter)
y_verhaeltnis=1.0
; Kurve: unter 1 = kleine Bewegungen staerker, ueber 1 = praeziser
kurve=0.75
; Totzone des Spiels ueberspringen (0.0 - 0.5). Wenn feines Zielen "haengt": erhoehen.
deadzone=0.12
; Glaettung in Millisekunden (kleiner = direkter, groesser = ruhiger)
glaettung_ms=14
; Maus hoch = Kamera runter (1 = ja)
y_invertieren=0
; Mausbewegung (Punkte pro Sekunde) fuer vollen Stickausschlag bei Empfindlichkeit 1.0
volle_geschwindigkeit=1500

[tasten]
; Eingabe = Controller-Taste (mehrere mit Komma trennen)
; Controller-Tasten: Cross, Circle, Square, Triangle, L1, R1, L2, R2, L3, R3,
;   Options, Share, Touchpad, PS, DpadUp, DpadDown, DpadLeft, DpadRight,
;   MoveUp, MoveDown, MoveLeft, MoveRight, Walk
; Maus: MouseLeft, MouseRight, MouseMiddle, MouseX1, MouseX2, WheelUp, WheelDown
W=MoveUp
S=MoveDown
A=MoveLeft
D=MoveRight
CapsLock=Walk
MouseLeft=R2
MouseRight=L2
Shift=Cross
Space=Square
F=Triangle
R=Circle
Q=R1
Tab=L1
Ctrl=L3
V=R3
MouseMiddle=R3
E=DpadRight
T=DpadUp
X=DpadDown
G=DpadLeft
MouseX1=DpadLeft
MouseX2=DpadRight
WheelUp=DpadUp
WheelDown=DpadDown
Up=DpadUp
Down=DpadDown
Left=DpadLeft
Right=DpadRight
M=Touchpad
Esc=Options
Enter=Cross
Backspace=Circle
P=PS
)INI";

MouseAim::MouseAim(QWindow *w, QObject *parent)
	: QObject(parent), window(w)
{
	chiaki_controller_state_set_idle(&state);
	clock.start();

	QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
	QDir().mkpath(dir);
	config_path = dir + "/gta-modus.ini";
	if(!QFile::exists(config_path))
		WriteDefaultConfig();
	LoadConfig();

	tick_timer.setTimerType(Qt::PreciseTimer);
	tick_timer.setInterval(4);
	connect(&tick_timer, &QTimer::timeout, this, &MouseAim::Tick);

	if(window)
	{
		window_active = window->isActive();
		connect(window, &QWindow::activeChanged, this, [this]() {
			window_active = window && window->isActive();
			UpdateActivation();
		});
	}
	QCoreApplication::instance()->installNativeEventFilter(this);
}

MouseAim::~MouseAim()
{
	QCoreApplication::instance()->removeNativeEventFilter(this);
	LockCursor(false);
}

void MouseAim::WriteDefaultConfig()
{
	QFile f(config_path);
	if(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
		f.write(DEFAULT_CONFIG);
}

int MouseAim::ParseInput(const QString &raw)
{
	const QString n = raw.trimmed().toLower();
	static const QHash<QString, int> special = {
		{"mouseleft", MOUSE_LEFT}, {"mouseright", MOUSE_RIGHT}, {"mousemiddle", MOUSE_MIDDLE},
		{"mousex1", MOUSE_X1}, {"mousex2", MOUSE_X2}, {"wheelup", WHEEL_UP}, {"wheeldown", WHEEL_DOWN},
		{"shift", Qt::Key_Shift}, {"umschalt", Qt::Key_Shift},
		{"ctrl", Qt::Key_Control}, {"strg", Qt::Key_Control}, {"control", Qt::Key_Control},
		{"alt", Qt::Key_Alt}, {"capslock", Qt::Key_CapsLock}, {"feststell", Qt::Key_CapsLock},
		{"space", Qt::Key_Space}, {"leertaste", Qt::Key_Space},
		{"tab", Qt::Key_Tab}, {"esc", Qt::Key_Escape}, {"escape", Qt::Key_Escape},
		{"enter", Qt::Key_Return}, {"return", Qt::Key_Return},
		{"backspace", Qt::Key_Backspace}, {"up", Qt::Key_Up}, {"down", Qt::Key_Down},
		{"left", Qt::Key_Left}, {"right", Qt::Key_Right},
	};
	if(special.contains(n))
		return special.value(n);
	QKeySequence seq = QKeySequence::fromString(raw.trimmed(), QKeySequence::PortableText);
	if(seq.isEmpty())
		return 0;
	return static_cast<int>(seq[0].key());
}

int MouseAim::ParseTarget(const QString &raw)
{
	static const QHash<QString, int> map = {
		{"cross", CHIAKI_CONTROLLER_BUTTON_CROSS}, {"x", CHIAKI_CONTROLLER_BUTTON_CROSS},
		{"circle", CHIAKI_CONTROLLER_BUTTON_MOON}, {"kreis", CHIAKI_CONTROLLER_BUTTON_MOON},
		{"square", CHIAKI_CONTROLLER_BUTTON_BOX}, {"quadrat", CHIAKI_CONTROLLER_BUTTON_BOX},
		{"triangle", CHIAKI_CONTROLLER_BUTTON_PYRAMID}, {"dreieck", CHIAKI_CONTROLLER_BUTTON_PYRAMID},
		{"l1", CHIAKI_CONTROLLER_BUTTON_L1}, {"r1", CHIAKI_CONTROLLER_BUTTON_R1},
		{"l3", CHIAKI_CONTROLLER_BUTTON_L3}, {"r3", CHIAKI_CONTROLLER_BUTTON_R3},
		{"options", CHIAKI_CONTROLLER_BUTTON_OPTIONS}, {"share", CHIAKI_CONTROLLER_BUTTON_SHARE},
		{"create", CHIAKI_CONTROLLER_BUTTON_SHARE},
		{"touchpad", CHIAKI_CONTROLLER_BUTTON_TOUCHPAD}, {"ps", CHIAKI_CONTROLLER_BUTTON_PS},
		{"dpadup", CHIAKI_CONTROLLER_BUTTON_DPAD_UP}, {"dpaddown", CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN},
		{"dpadleft", CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT}, {"dpadright", CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT},
		{"l2", T_L2}, {"r2", T_R2}, {"walk", T_WALK}, {"gehen", T_WALK},
		{"moveup", T_MOVE_UP}, {"movedown", T_MOVE_DOWN},
		{"moveleft", T_MOVE_LEFT}, {"moveright", T_MOVE_RIGHT},
	};
	const QString n = raw.trimmed().toLower();
	return map.value(n, 0);
}

void MouseAim::LoadConfig()
{
	QFile f(config_path);
	if(!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return;

	QHash<int, QList<int>> new_bindings;
	bool auto_enable = true;
	QString section;
	QTextStream in(&f);
	while(!in.atEnd())
	{
		QString line = in.readLine().trimmed();
		if(line.isEmpty() || line.startsWith(';') || line.startsWith('#'))
			continue;
		if(line.startsWith('[') && line.endsWith(']'))
		{
			section = line.mid(1, line.size() - 2).trimmed().toLower();
			continue;
		}
		const int eq = line.indexOf('=');
		if(eq <= 0)
			continue;
		const QString key = line.left(eq).trimmed();
		const QString value = line.mid(eq + 1).trimmed();
		bool ok = false;
		const double num = value.toDouble(&ok);

		if(section == "maus")
		{
			const QString k = key.toLower();
			if(!ok) continue;
			if(k == "empfindlichkeit") params.sensitivity = std::clamp(num, 0.05, 20.0);
			else if(k == "y_verhaeltnis") params.y_ratio = std::clamp(num, 0.05, 5.0);
			else if(k == "kurve") params.exponent = std::clamp(num, 0.1, 3.0);
			else if(k == "deadzone") params.deadzone = std::clamp(num, 0.0, 0.6);
			else if(k == "glaettung_ms") params.smoothing_ms = std::clamp(num, 4.0, 100.0);
			else if(k == "y_invertieren") params.invert_y = num != 0.0;
			else if(k == "volle_geschwindigkeit") params.full_speed_counts = std::clamp(num, 50.0, 100000.0);
		}
		else if(section == "allgemein")
		{
			const QString k = key.toLower();
			if(!ok) continue;
			if(k == "aktiv_beim_start") auto_enable = num != 0.0;
			else if(k == "gehen_faktor") walk_factor = std::clamp(num, 0.05, 1.0);
		}
		else if(section == "tasten")
		{
			const int input = ParseInput(key);
			if(!input)
			{
				qWarning() << "GTA-Modus: unbekannte Eingabe" << key;
				continue;
			}
			for(const QString &t : value.split(',', Qt::SkipEmptyParts))
			{
				const int target = ParseTarget(t);
				if(target)
					new_bindings[input].append(target);
				else
					qWarning() << "GTA-Modus: unbekannte Controller-Taste" << t;
			}
		}
	}
	if(!new_bindings.isEmpty())
		bindings = new_bindings;
	static bool first_load = true;
	if(first_load)
	{
		enabled = auto_enable;
		first_load = false;
	}
}

void MouseAim::SaveSensitivity()
{
	QFile f(config_path);
	if(!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	QString text = QString::fromUtf8(f.readAll());
	f.close();
	static const QRegularExpression re("^empfindlichkeit\\s*=.*$", QRegularExpression::MultilineOption);
	const QString line = QString("empfindlichkeit=%1").arg(params.sensitivity, 0, 'f', 2);
	if(text.contains(re))
		text.replace(re, line);
	else
		text += "\n[maus]\n" + line + "\n";
	if(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
		f.write(text.toUtf8());
}

void MouseAim::SetSessionRunning(bool running)
{
	session_running = running;
	UpdateActivation();
}

void MouseAim::SetSuspended(bool s)
{
	suspended = s;
	UpdateActivation();
}

void MouseAim::Toggle()
{
	enabled = !enabled;
	if(enabled)
		LoadConfig();
	emit StatusMessage(enabled ? QStringLiteral("GTA-Modus AN (F8 = aus)") : QStringLiteral("GTA-Modus AUS (F8 = an)"));
	UpdateActivation();
}

void MouseAim::UpdateActivation()
{
	const bool active = IsActive();
	if(active == cursor_locked)
		return;

	if(active)
	{
		curve.Reset();
		raw_input_seen = false;
#ifdef Q_OS_WIN
		timeBeginPeriod(1);
		if(window)
		{
			RAWINPUTDEVICE rid;
			rid.usUsagePage = 0x01; // generic desktop
			rid.usUsage = 0x02;     // mouse
			rid.dwFlags = 0;
			rid.hwndTarget = reinterpret_cast<HWND>(window->winId());
			raw_input_registered = RegisterRawInputDevices(&rid, 1, sizeof(rid));
		}
#endif
		LockCursor(true);
		tick_timer.start();
	}
	else
	{
		tick_timer.stop();
		LockCursor(false);
#ifdef Q_OS_WIN
		if(raw_input_registered)
		{
			RAWINPUTDEVICE rid;
			rid.usUsagePage = 0x01;
			rid.usUsage = 0x02;
			rid.dwFlags = RIDEV_REMOVE;
			rid.hwndTarget = nullptr;
			RegisterRawInputDevices(&rid, 1, sizeof(rid));
			raw_input_registered = false;
		}
		timeEndPeriod(1);
#endif
		// release everything so nothing stays pressed in the game
		target_count.clear();
		held_inputs.clear();
		wheel_release_at.clear();
		curve.Reset();
		right_stick = StickOut();
		RebuildState();
	}
}

void MouseAim::LockCursor(bool lock)
{
	cursor_locked = lock;
	if(!window)
		return;
	if(lock)
	{
		window->setCursor(Qt::BlankCursor);
		RecenterCursor();
	}
	else
	{
#ifdef Q_OS_WIN
		ClipCursor(nullptr);
#endif
		window->setCursor(Qt::ArrowCursor);
	}
}

void MouseAim::RecenterCursor()
{
	if(!window)
		return;
#ifdef Q_OS_WIN
	HWND hwnd = reinterpret_cast<HWND>(window->winId());
	RECT rc;
	if(GetClientRect(hwnd, &rc))
	{
		POINT c = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
		ClientToScreen(hwnd, &c);
		RECT clip = { c.x, c.y, c.x + 1, c.y + 1 };
		ClipCursor(&clip);
		SetCursorPos(c.x, c.y);
	}
#else
	const QPoint center(window->width() / 2, window->height() / 2);
	QCursor::setPos(window->screen(), window->mapToGlobal(center));
#endif
}

bool MouseAim::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
	Q_UNUSED(result);
#ifdef Q_OS_WIN
	if(!cursor_locked || eventType != "windows_generic_MSG")
		return false;
	MSG *msg = static_cast<MSG *>(message);
	if(msg->message != WM_INPUT)
		return false;
	RAWINPUT raw;
	UINT size = sizeof(raw);
	if(GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
		return false;
	if(raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
	{
		if(raw.data.mouse.lLastX || raw.data.mouse.lLastY)
		{
			raw_input_seen = true;
			AddMovement(raw.data.mouse.lLastX, raw.data.mouse.lLastY);
		}
	}
#else
	Q_UNUSED(eventType);
	Q_UNUSED(message);
#endif
	return false;
}

void MouseAim::AddMovement(double dx, double dy)
{
	curve.AddMovement(static_cast<double>(clock.nsecsElapsed()) / 1e6, dx, dy);
}

void MouseAim::Tick()
{
	if(!cursor_locked)
		return;
#ifdef Q_OS_WIN
	// Windows can drop the clip rect (alt-tab, resolution change...), so re-apply it
	if(window)
	{
		static int n = 0;
		if(++n % 25 == 0)
			RecenterCursor();
	}
#endif
	const double now = static_cast<double>(clock.nsecsElapsed()) / 1e6;
	bool changed = false;

	for(auto it = wheel_release_at.begin(); it != wheel_release_at.end();)
	{
		if(now >= it.value())
		{
			const int code = it.key();
			it = wheel_release_at.erase(it);
			for(int t : bindings.value(code))
				ApplyTarget(t, false);
			changed = true;
		}
		else
			++it;
	}

	const StickOut s = curve.Compute(now, params);
	if(s.x != right_stick.x || s.y != right_stick.y)
	{
		right_stick = s;
		changed = true;
	}
	if(changed)
		RebuildState();
}

void MouseAim::ApplyTarget(int target, bool down)
{
	int &c = target_count[target];
	c = down ? c + 1 : (std::max)(0, c - 1);
}

void MouseAim::InputDown(int code)
{
	if(held_inputs.contains(code))
		return;
	held_inputs.insert(code);
	const auto targets = bindings.value(code);
	for(int t : targets)
		ApplyTarget(t, true);
	if(!targets.isEmpty())
		RebuildState();
}

void MouseAim::InputUp(int code)
{
	if(!held_inputs.remove(code))
		return;
	const auto targets = bindings.value(code);
	for(int t : targets)
		ApplyTarget(t, false);
	if(!targets.isEmpty())
		RebuildState();
}

void MouseAim::RebuildState()
{
	ChiakiControllerState s;
	chiaki_controller_state_set_idle(&s);
	auto held = [this](int t) { return target_count.value(t, 0) > 0; };

	for(auto it = target_count.cbegin(); it != target_count.cend(); ++it)
	{
		if(it.value() <= 0)
			continue;
		if(it.key() > 0)
			s.buttons |= static_cast<uint32_t>(it.key());
	}
	if(held(T_L2)) s.l2_state = 0xff;
	if(held(T_R2)) s.r2_state = 0xff;

	double lx = (held(T_MOVE_RIGHT) ? 1.0 : 0.0) - (held(T_MOVE_LEFT) ? 1.0 : 0.0);
	double ly = (held(T_MOVE_DOWN) ? 1.0 : 0.0) - (held(T_MOVE_UP) ? 1.0 : 0.0);
	if(lx != 0.0 && ly != 0.0)
	{
		lx *= 0.70710678;
		ly *= 0.70710678;
	}
	if(held(T_WALK))
	{
		lx *= walk_factor;
		ly *= walk_factor;
	}
	s.left_x = static_cast<int16_t>(std::lround(lx * 32767.0));
	s.left_y = static_cast<int16_t>(std::lround(ly * 32767.0));
	s.right_x = right_stick.x;
	s.right_y = right_stick.y;

	state = s;
	emit StateChanged();
}

static int MouseButtonCode(Qt::MouseButton b)
{
	switch(b)
	{
		case Qt::LeftButton: return 0x7f000001;
		case Qt::RightButton: return 0x7f000002;
		case Qt::MiddleButton: return 0x7f000003;
		case Qt::BackButton: return 0x7f000004;
		case Qt::ForwardButton: return 0x7f000005;
		default: return 0;
	}
}

bool MouseAim::HandleKey(QKeyEvent *event)
{
	const bool press = event->type() == QEvent::KeyPress;

	// global hotkeys work whenever a stream is running
	if(press && session_running && !suspended && event->modifiers() == Qt::NoModifier)
	{
		if(event->key() == Qt::Key_F8)
		{
			if(!event->isAutoRepeat())
				Toggle();
			return true;
		}
		if(enabled && (event->key() == Qt::Key_F6 || event->key() == Qt::Key_F7))
		{
			const double step = event->key() == Qt::Key_F7 ? 1.1 : 1.0 / 1.1;
			params.sensitivity = std::clamp(params.sensitivity * step, 0.05, 20.0);
			SaveSensitivity();
			emit StatusMessage(QString("GTA-Modus Empfindlichkeit: %1").arg(params.sensitivity, 0, 'f', 2));
			return true;
		}
	}
	if(!IsActive())
		return false;
	if(event->key() == Qt::Key_F8 || event->key() == Qt::Key_F6 || event->key() == Qt::Key_F7)
		return true;

	// Qt reports Shift/Ctrl as modifiers too; key() is still the plain key
	int code = event->key();
	if(code == Qt::Key_Backtab)
		code = Qt::Key_Tab;
	if(!bindings.contains(code))
		return true; // swallow unbound keys so they do not trigger chiaki's own mapping
	if(event->isAutoRepeat())
		return true;
	if(press)
		InputDown(code);
	else
		InputUp(code);
	return true;
}

bool MouseAim::HandleMousePress(QMouseEvent *event)
{
	if(!IsActive())
		return false;
	const int code = MouseButtonCode(event->button());
	if(code)
		InputDown(code);
	return true;
}

bool MouseAim::HandleMouseRelease(QMouseEvent *event)
{
	if(!IsActive())
		return false;
	const int code = MouseButtonCode(event->button());
	if(code)
		InputUp(code);
	return true;
}

bool MouseAim::HandleMouseMove(QMouseEvent *event)
{
	if(!IsActive())
		return false;
#ifndef Q_OS_WIN
	// Fallback without raw input: measure distance from center and re-center
	if(window)
	{
		const QPointF center(window->width() / 2.0, window->height() / 2.0);
		const QPointF d = event->position() - center;
		if(std::abs(d.x()) >= 1.0 || std::abs(d.y()) >= 1.0)
		{
			const qreal dpr = window->devicePixelRatio();
			AddMovement(d.x() * dpr, d.y() * dpr);
			RecenterCursor();
		}
	}
#else
	Q_UNUSED(event);
#endif
	return true;
}

bool MouseAim::HandleWheel(QWheelEvent *event)
{
	if(!IsActive())
		return false;
	const int dy = event->angleDelta().y();
	if(dy == 0)
		return true;
	const int code = dy > 0 ? WHEEL_UP : WHEEL_DOWN;
	if(!bindings.contains(code) || wheel_release_at.contains(code))
		return true;
	for(int t : bindings.value(code))
		ApplyTarget(t, true);
	wheel_release_at[code] = static_cast<int>(clock.elapsed()) + wheel_pulse_ms;
	RebuildState();
	return true;
}
