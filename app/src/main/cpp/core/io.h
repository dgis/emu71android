/*
 *   io.h
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gieﬂelink
 *
 */

// register I/O addresses
#define ANNAD1		0x2E100					// Annunciator [<- - - -]
#define ANN1_5		0x2E101					// Annunciator [- RAD USER AC]
#define ANNAD2		0x2E102					// Annunciator [g f - -]
#define ANN2_5		0x2E103					// Annunciator [- - - BAT]
#define DD3ST		0x2E104					// SLAVE3 start display RAM
#define DD3END		0x2E160					// SLAVE3 end display RAM
#define TIMER3		0x2E1F8					// Timer 3 (6 nibbles)
#define DD3CTL		0x2E1FF					// Display-Timer Control 3
#define DD2ST		0x2E200					// SLAVE2 start display RAM
#define DD2END		0x2E260					// SLAVE2 end display RAM
#define TIMER2		0x2E2F8					// Timer 2 (6 nibbles)
#define DD2CTL		0x2E2FF					// Display-Timer Control 2
#define DD1ST		0x2E300					// SLAVE1 start display RAM
#define DD1END		0x2E34C					// SLAVE1 end display RAM
#define ANNAD3		0x2E34C					// Annunciator [0 - - -]
#define ANN3_5		0x2E34D					// Annunciator [4 3 2 1]
#define ANNAD4		0x2E34E					// Annunciator [((*)) - - -]
#define ANN4_5		0x2E34F					// Annunciator [CALC SUSP PRGM ->]
#define ROWDVR		0x2E350					// 16 nibbles display row driver
#define TIMER1		0x2E3F8					// Timer 1 (6 nibbles)
#define DCONTR		0x2E3FE					// Contrast Control
#define DD1CTL		0x2E3FF					// Display-Timer Control 1

// 0x2E3FE Contrast Control Nibble [CONT3 CONT2 CONT1 CONT0]
#define CONT3		0x08					// Display CONTrast Bit3
#define CONT2		0x04					// Display CONTrast Bit2
#define CONT1		0x02					// Display CONTrast Bit1
#define CONT0		0x01					// Display CONTrast Bit0

// 0x2EnFF Display-Timer Control Nibble [LBI/WKE VLBI/DTEST DBLINK DON]
#define LBI			0x08					// Low Battery Indicator (Read only)
#define WKE			0x08					// WaKE up (Write only)
#define VLBI		0x04					// Very Low Battery Indicator (Read only)
#define DTEST		0x04					// Display TEST (Write only)
#define DBLINK		0x02					// Display BLINK
#define DON			0x01					// Display ON
