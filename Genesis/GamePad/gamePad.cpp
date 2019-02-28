#include "gamePad.hpp"

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
}

bool GamePad::Update(sfEvent event)
{
	if(event.type == sfEvtKeyPressed)
  	{   
  		switch(event.key.code)
  		{
  			//Manette 1
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

void GamePad::ButtonPressed(int gamepad, int button)
{
	BitSet(get().buttonState[gamepad], button);
}

void GamePad::ButtonReleased(int gamepad, int button)
{
	BitReset(get().buttonState[gamepad], button);
}

void GamePad::Write(int gamepad, int data, bool control)
{
	if(!control)
	{
		get().dataReg[gamepad] = data;
	}
	else
	{
		get().controlReg[gamepad] = data;
	}

	byte mask = get().controlReg[gamepad];

	get().gamepadState[gamepad] &= ~mask;

	get().gamepadState[gamepad] |= data & mask;
}

byte GamePad::ReadData(int gamepad)
{

	byte data = get().gamepadState[gamepad] & 0x40;
	data |= 0x3F;

	if(data & 0x40)
	{
		data &= ~(get().buttonState[gamepad] & 0x3F);
	}
	else
	{
		data &= ~(0xC | (get().buttonState[gamepad] & 0x3) | ((get().buttonState[gamepad] >> 2) & 0x30));
	}

	byte mask = 0x80 | get().controlReg[gamepad];
	byte value = get().dataReg[gamepad] & mask;
	value |= data & ~mask;

	return value;
}

byte GamePad::ReadControl(int gamepad)
{
	return get().controlReg[gamepad];
}