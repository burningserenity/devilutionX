#include "miniwin/misc_msg.h"

#include <cstdint>
#include <deque>
#include <string>

#include <SDL.h>

#include "control.h"
#include "controls/controller.h"
#include "controls/input.h"
#include "controls/plrctrls.h"
#ifndef USE_SDL1
#include "controls/touch/event_handlers.h"
#endif
#include "cursor.h"
#include "engine/demomode.h"
#include "engine/rectangle.hpp"
#include "hwcursor.hpp"
#include "movie.h"
#include "panels/spell_list.hpp"
#include "qol/stash.h"
#include "utils/display.h"
#include "utils/log.hpp"
#include "utils/utf8.hpp"

#ifdef __vita__
#include "platform/vita/touch.h"
#endif

#ifdef __SWITCH__
#include "platform/switch/docking.h"
#include <switch.h>
#endif

/** @file
 * *
 * Windows message handling and keyboard event conversion for SDL.
 */

namespace devilution {

void SetMouseButtonEvent(SDL_Event &event, uint32_t type, uint8_t button, Point position)
{
	event.type = type;
	event.button.button = button;
	if (type == SDL_MOUSEBUTTONDOWN) {
		event.button.state = SDL_PRESSED;
	} else {
		event.button.state = SDL_RELEASED;
	}
	event.button.x = position.x;
	event.button.y = position.y;
}

void SetCursorPos(Point position)
{
	if (ControlDevice != ControlTypes::KeyboardAndMouse) {
		MousePosition = position;
		return;
	}

	LogicalToOutput(&position.x, &position.y);
	if (!demo::IsRunning())
		SDL_WarpMouseInWindow(ghMainWnd, position.x, position.y);
}

// Moves the mouse to the first attribute "+" button.
void FocusOnCharInfo()
{
	Player &myPlayer = *MyPlayer;

	if (invflag || myPlayer._pStatPts <= 0)
		return;

	// Find the first incrementable stat.
	int stat = -1;
	for (auto attribute : enum_values<CharacterAttribute>()) {
		if (myPlayer.GetBaseAttributeValue(attribute) >= myPlayer.GetMaximumAttributeValue(attribute))
			continue;
		stat = static_cast<int>(attribute);
	}
	if (stat == -1)
		return;

	SetCursorPos(ChrBtnsRect[stat].Center());
}

namespace {

bool FalseAvail(const char *name, int value)
{
	LogVerbose("Unhandled SDL event: {} {}", name, value);
	return true;
}

} // namespace

bool FetchMessage_Real(SDL_Event *event, uint16_t *modState)
{
#ifdef __SWITCH__
	HandleDocking();
#endif

	SDL_Event e;
	if (PollEvent(&e) == 0) {
		return false;
	}

	event->type = static_cast<SDL_EventType>(0);
	*modState = SDL_GetModState();

#ifdef __vita__
	HandleTouchEvent(&e, MousePosition);
#elif !defined(USE_SDL1)
	HandleTouchEvent(e);
#endif

	if (e.type == SDL_QUIT || IsCustomEvent(e.type)) {
		*event = e;
		return true;
	}

	if (IsAnyOf(e.type, SDL_KEYUP, SDL_KEYDOWN) && e.key.keysym.sym == SDLK_UNKNOWN) {
		// Erroneous events generated by RG350 kernel.
		return true;
	}

#if !defined(USE_SDL1) && !defined(__vita__)
	if (!movie_playing) {
		// SDL generates mouse events from touch-based inputs to provide basic
		// touchscreeen support for apps that don't explicitly handle touch events
		if (IsAnyOf(e.type, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP) && e.button.which == SDL_TOUCH_MOUSEID)
			return true;
		if (e.type == SDL_MOUSEMOTION && e.motion.which == SDL_TOUCH_MOUSEID)
			return true;
		if (e.type == SDL_MOUSEWHEEL && e.wheel.which == SDL_TOUCH_MOUSEID)
			return true;
	}
#endif

#ifdef USE_SDL1
	if (e.type == SDL_MOUSEMOTION) {
		OutputToLogical(&e.motion.x, &e.motion.y);
	} else if (IsAnyOf(e.type, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP)) {
		OutputToLogical(&e.button.x, &e.button.y);
	}
#endif

	if (HandleControllerAddedOrRemovedEvent(e))
		return true;

	switch (e.type) {
#ifndef USE_SDL1
	case SDL_CONTROLLERAXISMOTION:
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
	case SDL_FINGERDOWN:
	case SDL_FINGERUP:
#endif
	case SDL_JOYAXISMOTION:
	case SDL_JOYHATMOTION:
	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP:
		*event = e;
		break;
	case SDL_KEYDOWN:
	case SDL_KEYUP: {
#ifdef USE_SDL1
		if (gbRunGame && (IsTalkActive() || dropGoldFlag)) {
			Uint16 unicode = e.key.keysym.unicode;
			if (unicode >= ' ') {
				std::string utf8;
				AppendUtf8(unicode, utf8);
				if (IsTalkActive())
					control_new_text(utf8);
				if (dropGoldFlag)
					GoldDropNewText(utf8);
			}
		}
#endif
		if (e.key.keysym.sym == -1)
			return FalseAvail(e.type == SDL_KEYDOWN ? "SDL_KEYDOWN" : "SDL_KEYUP", e.key.keysym.sym);
		*event = e;
	} break;
	case SDL_MOUSEMOTION:
		*event = e;
		if (ControlMode == ControlTypes::KeyboardAndMouse && invflag)
			InvalidateInventorySlot();
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		*event = e;
		break;
#ifndef USE_SDL1
	case SDL_MOUSEWHEEL:
		event->type = SDL_KEYDOWN;
		if (e.wheel.y > 0) {
			event->key.keysym.sym = (SDL_GetModState() & KMOD_CTRL) != 0 ? SDLK_KP_PLUS : SDLK_UP;
		} else if (e.wheel.y < 0) {
			event->key.keysym.sym = (SDL_GetModState() & KMOD_CTRL) != 0 ? SDLK_KP_MINUS : SDLK_DOWN;
		} else if (e.wheel.x > 0) {
			event->key.keysym.sym = SDLK_LEFT;
		} else if (e.wheel.x < 0) {
			event->key.keysym.sym = SDLK_RIGHT;
		}
		break;
#if SDL_VERSION_ATLEAST(2, 0, 4)
	case SDL_AUDIODEVICEADDED:
		return FalseAvail("SDL_AUDIODEVICEADDED", e.adevice.which);
	case SDL_AUDIODEVICEREMOVED:
		return FalseAvail("SDL_AUDIODEVICEREMOVED", e.adevice.which);
	case SDL_KEYMAPCHANGED:
		return FalseAvail("SDL_KEYMAPCHANGED", 0);
#endif
	case SDL_TEXTEDITING:
		if (gbRunGame)
			break;
		return FalseAvail("SDL_TEXTEDITING", e.edit.length);
	case SDL_TEXTINPUT:
		if (gbRunGame && IsTalkActive()) {
			control_new_text(e.text.text);
			break;
		}
		if (gbRunGame && dropGoldFlag) {
			GoldDropNewText(e.text.text);
			break;
		}
		if (gbRunGame && IsWithdrawGoldOpen) {
			GoldWithdrawNewText(e.text.text);
			break;
		}
		return FalseAvail("SDL_TEXTINPUT", e.text.windowID);
	case SDL_WINDOWEVENT:
		*event = e;
		break;
#else
	case SDL_ACTIVEEVENT:
		*event = e;
		break;
#endif
	default:
		return FalseAvail("unknown", e.type);
	}
	return true;
}

bool FetchMessage(SDL_Event *event, uint16_t *modState)
{
	const bool available = demo::IsRunning() ? demo::FetchMessage(event, modState) : FetchMessage_Real(event, modState);

	if (available && demo::IsRecording())
		demo::RecordMessage(*event, *modState);

	return available;
}

void HandleMessage(const SDL_Event &event, uint16_t modState)
{
	assert(CurrentEventHandler != nullptr);

	CurrentEventHandler(event, modState);
}

} // namespace devilution
