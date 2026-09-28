#ifndef __msettings_h__
#define __msettings_h__

//////////////////////////////////////
// Settings Lifecycle

void InitSettings(void);
void QuitSettings(void);

//////////////////////////////////////
// Display & Audio Controls

int GetBrightness(void);
int GetVolume(void);

void SetRawBrightness(int value); // 0-255
void SetRawVolume(int value);	  // 0-100

void SetBrightness(int value); // 0-10
void SetVolume(int value);	   // 0-20

//////////////////////////////////////
// Peripheral & Power State

int GetJack(void);
void SetJack(int value); // 0-1

int GetHDMI(void);
void SetHDMI(int value); // 0-1

int GetMute(void);
void SetMute(int value); // 0-1

int GetCharging(void);
void SetCharging(int value); // 0-1

int GetBattery(void);
void SetBattery(int value); // 0-100

int GetBT(void);
void SetBT(int value); // 0-1

#endif
