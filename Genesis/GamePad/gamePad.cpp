#include "gamePad.hpp"
#include <algorithm>

GamePad::GamePad()
{
	
}

GamePad& GamePad::get()
{
	static GamePad instance;
	return instance;
}

void GamePad::Init()
{
	get().dataReg[0] = 0x7F;
	get().dataReg[1] = 0x7F;

	get().controlReg[0] = 0x0;
	get().controlReg[1] = 0x0;

	get().buttonState[0] = 0x0;
	get().buttonState[1] = 0x0;

	get().gamepadState[0] = 0x0;
	get().gamepadState[1] = 0x0;
    for (unsigned i=0; i<2; ++i) { get().sixButton[i]=true; get().phase[i]=0; get().idle[i]=0; }
}

#ifndef DRIVONIX_HEADLESS
bool GamePad::Update(sfEvent event)
{
	if(event.type == sfEvtKeyPressed)
  	{   
  		switch(event.key.code)
  		{
  			//Manette 1
                case sfKeyI: get().ButtonPressed(0, MD_X); break;
                case sfKeyO: get().ButtonPressed(0, MD_Y); break;
                case sfKeyP: get().ButtonPressed(0, MD_Z); break;
                case sfKeyReturn: get().ButtonPressed(0, MD_MODE); break;
  			case sfKeyZ:
  			get().ButtonPressed(0, MD_UP);
  			break;

  			case sfKeyS:
  			get().ButtonPressed(0, MD_DOWN);
  			break;

  			case sfKeyQ:
  			get().ButtonPressed(0, MD_LEFT);
  			break;

  			case sfKeyD:
  			get().ButtonPressed(0, MD_RIGHT);
  			break;

  			case sfKeyA:
  			get().ButtonPressed(0, MD_START);
  			break;

  			case sfKeyK:
  			get().ButtonPressed(0, MD_A);
  			break;

  			case sfKeyL:
  			get().ButtonPressed(0, MD_B);
  			break;

  			case sfKeyM:
  			get().ButtonPressed(0, MD_C);
  			break;

  			//Manette 2
                case sfKeyNumpad7: get().ButtonPressed(1, MD_X); break;
                case sfKeyNumpad8: get().ButtonPressed(1, MD_Y); break;
                case sfKeyNumpad9: get().ButtonPressed(1, MD_Z); break;
                case sfKeyNumpad0: get().ButtonPressed(1, MD_MODE); break;
  			case sfKeyUp:
  			get().ButtonPressed(1, MD_UP);
  			break;

  			case sfKeyDown:
  			get().ButtonPressed(1, MD_DOWN);
  			break;

  			case sfKeyLeft:
  			get().ButtonPressed(1, MD_LEFT);
  			break;

  			case sfKeyRight:
  			get().ButtonPressed(1, MD_RIGHT);
  			break;

  			case sfKeyNumpad4:
  			get().ButtonPressed(1, MD_START);
  			break;

  			case sfKeyNumpad1:
  			get().ButtonPressed(1, MD_A);
  			break;

  			case sfKeyNumpad2:
  			get().ButtonPressed(1, MD_B);
  			break;

  			case sfKeyNumpad3:
  			get().ButtonPressed(1, MD_C);
  			break;

  			case sfKeyEscape:
  			return true;
  			break;
  		}
  	}
  	else if(event.type == sfEvtKeyReleased)
  	{
	    switch(event.key.code)
  		{
  			//Manette 1
                case sfKeyI: get().ButtonReleased(0, MD_X); break;
                case sfKeyO: get().ButtonReleased(0, MD_Y); break;
                case sfKeyP: get().ButtonReleased(0, MD_Z); break;
                case sfKeyReturn: get().ButtonReleased(0, MD_MODE); break;
  			case sfKeyZ:
  			get().ButtonReleased(0, MD_UP);
  			break;

  			case sfKeyS:
  			get().ButtonReleased(0, MD_DOWN);
  			break;

  			case sfKeyQ:
  			get().ButtonReleased(0, MD_LEFT);
  			break;

  			case sfKeyD:
  			get().ButtonReleased(0, MD_RIGHT);
  			break;

  			case sfKeyA:
  			get().ButtonReleased(0, MD_START);
  			break;

  			case sfKeyK:
  			get().ButtonReleased(0, MD_A);
  			break;

  			case sfKeyL:
  			get().ButtonReleased(0, MD_B);
  			break;

  			case sfKeyM:
  			get().ButtonReleased(0, MD_C);
  			break;

  			//Manette 2
                case sfKeyNumpad7: get().ButtonReleased(1, MD_X); break;
                case sfKeyNumpad8: get().ButtonReleased(1, MD_Y); break;
                case sfKeyNumpad9: get().ButtonReleased(1, MD_Z); break;
                case sfKeyNumpad0: get().ButtonReleased(1, MD_MODE); break;
  			case sfKeyUp:
  			get().ButtonReleased(1, MD_UP);
  			break;

  			case sfKeyDown:
  			get().ButtonReleased(1, MD_DOWN);
  			break;

  			case sfKeyLeft:
  			get().ButtonReleased(1, MD_LEFT);
  			break;

  			case sfKeyRight:
  			get().ButtonReleased(1, MD_RIGHT);
  			break;

  			case sfKeyNumpad4:
  			get().ButtonReleased(1, MD_START);
  			break;

  			case sfKeyNumpad1:
  			get().ButtonReleased(1, MD_A);
  			break;

  			case sfKeyNumpad2:
  			get().ButtonReleased(1, MD_B);
  			break;

  			case sfKeyNumpad3:
  			get().ButtonReleased(1, MD_C);
  			break;

  			case sfKeyEscape:
  			return true;
  			break;
  		}
  	}

  	return false;
}

#endif

void GamePad::ButtonPressed(int gamepad, int button)
{
	BitSet(get().buttonState[gamepad], button);
}

void GamePad::ButtonReleased(int gamepad, int button)
{
	BitReset(get().buttonState[gamepad], button);
}

void GamePad::SetButton(int pad, int button, bool pressed) {
    if (pad < 0 || pad > 1 || button < 0 || button > MD_MODE) return;
    if (pressed) get().ButtonPressed(pad, button); else get().ButtonReleased(pad, button);
}
void GamePad::SetSixButton(int pad, bool enabled) {
    if (pad >= 0 && pad < 2) { get().sixButton[pad]=enabled; get().phase[pad]=0; }
}
void GamePad::Advance(unsigned ticks) {
    for (unsigned pad=0; pad<2; ++pad) {
        get().idle[pad]=std::min<unsigned>(get().idle[pad]+ticks, 100000);
        if (get().idle[pad]>=82000) get().phase[pad]=0; // Approximately 1.5 ms.
    }
}
void GamePad::Write(int pad, int data, bool control) {
    if (pad < 0 || pad > 1) return;
    auto& s=get();
    const bool oldTH=(s.controlReg[pad]&0x40) ? (s.dataReg[pad]&0x40)!=0 : true;
    if (control) s.controlReg[pad]=byte(data); else s.dataReg[pad]=byte(data);
    const bool newTH=(s.controlReg[pad]&0x40) ? (s.dataReg[pad]&0x40)!=0 : true;
    if (!oldTH && newTH) { s.phase[pad]=(s.phase[pad]+1)&3; s.idle[pad]=0; }
}
byte GamePad::ReadData(int pad) {
    if (pad < 0 || pad > 1) return 0x7f;
    auto& s=get();
    const bool th=(s.controlReg[pad]&0x40) ? (s.dataReg[pad]&0x40)!=0 : true;
    const unsigned buttons=s.buttonState[pad];
    byte data=0x3f;
    if (th) {
        if (s.sixButton[pad] && s.phase[pad]==3) {
            data &= byte(~((buttons&0x30) | ((buttons>>8)&1)<<2 | ((buttons>>9)&1)<<1 | ((buttons>>10)&1) | ((buttons>>11)&1)<<3));
        } else data &= byte(~(buttons&0x3f));
    } else {
        data &= byte(~(0x0c | (buttons&3) | ((buttons>>2)&0x30)));
        if (s.sixButton[pad] && s.phase[pad]==2) data &= 0x30;
        else if (s.sixButton[pad] && s.phase[pad]==3) data |= 0x0f;
    }
    if (th) data |= 0x40;
    const byte outputs=s.controlReg[pad];
    return byte((s.dataReg[pad]&outputs) | (data&~outputs));
}
byte GamePad::ReadControl(int pad) { return pad >= 0 && pad < 2 ? get().controlReg[pad] : 0; }
