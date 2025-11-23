; cdready - Check whether a CD-ROM drive has readable media
; Copyright (C) 2025  Petteri Valkonen <petteri.valkonen@iki.fi>
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <https://www.gnu.org/licenses/>.
;
; The MSCDEX driver and CD-ROM checks were gleaned from the CD-ROM
; programming FAQ at <https://www.flipcode.com/documents/cdromfaq.txt>.

ORG     100h            ; COM file format

; Get drive letter from command line
MOV     SI, 80h         ; Point to command line length
MOV     CL, [SI]        ; Get length
OR      CL, CL          ; Check if empty
JZ      Usage           ; If no parameters, show usage

INC     SI              ; Skip length byte
MOV     AL, [SI+1]      ; Get first character (skip space)

; Convert drive letter to drive number (A = 0, B = 1 etc.)
AND     AL, 0DFh        ; Convert to uppercase
SUB     AL, 'A'         ; Convert to drive number
JC      Usage           ; Invalid if < 'A'
CMP     AL, 25          ; Check if > 'Z'
JA      Usage

MOV     [DriveNum], AL  ; Save drive number

; Check if MSCDEX is installed
MOV     AX, 1500h       ; MSCDEX installation check
XOR     BX, BX          ; Clear BX
INT     2Fh
CMP     BX, 0           ; BX = 0 means not installed
JE      NoMSCDEX        ; MSCDEX not installed

; Check if this drive is a CD-ROM drive
MOV     AX, 150Bh       ; Check if drive is CD-ROM
MOV     CL, [DriveNum]  ; CL = drive number (0-based)
INT     2Fh
CMP     BX, 0ADADh      ; Magic return value if it's a CD-ROM
JNE     NotCDROM        ; Not a CD-ROM drive
CMP     AX, 0           ; AX != 0 means it's a CD-ROM
JE      NotCDROM

; Save old critical error handler
MOV     AX, 3524h       ; Get INT 24h vector
INT     21h
MOV     [OldInt24], BX
MOV     [OldInt24+2], ES

; Install our critical error handler
MOV     AX, 2524h       ; Set INT 24h vector
MOV     DX, CritErrHandler
INT     21h

; Build path string with actual drive letter
MOV     AL, [DriveNum]
ADD     AL, 'A'         ; Convert to letter
MOV     [DrivePath], AL ; Set drive letter

; Try to access root directory via DOS
MOV     AX, 4E00h       ; Find first file
MOV     CX, 0010h       ; Directory attribute
MOV     DX, DrivePath   ; Point to constructed path
INT     21h
PUSHF                   ; Save flags (carry indicates error)

; Restore old critical error handler
PUSH    DS
MOV     AX, 2524h       ; Set INT 24h vector
LDS     DX, [OldInt24]
INT     21h
POP     DS

POPF                    ; Restore flags
JC      NotReady        ; If error, drive not ready
        
Ready:
MOV     DX, ReadyMsg
MOV     AH, 09h         ; Print string
INT     21h
MOV     AL, 0           ; No error
JMP     Exit

NoMSCDEX:
MOV     DX, NoMSCDEXMsg
MOV     AH, 09h         ; Print string
INT     21h
MOV     AL, 2           ; MSCDEX not installed
JMP     Exit

NotReady:
MOV     DX, NotReadyMsg
MOV     AH, 09h         ; Print string
INT     21h
MOV     AL, 3           ; Media not ready
JMP     Exit

NotCDROM:
MOV     DX, NotCDROMMsg
MOV     AH, 09h         ; Print string
INT     21h
MOV     AL, 4           ; Not a CD-ROM drive
JMP     Exit
        
Usage:
MOV     DX, UsageMsg
MOV     AH, 09h         ; Print string
INT     21h
MOV     AL, 1           ; Bad usage
        
Exit:
MOV     AH, 4Ch         ; Terminate with return code
INT     21h

; Critical error handler - always return fail
CritErrHandler:
MOV     AL, 3           ; Fail
IRET

UsageMsg:
DB      'Usage: cdready <drive letter>', 0Dh, 0Ah
DB      'Returns: 0 = Ready, 1 = Bad usage, 2 = No MSCDEX,', 0Dh, 0Ah
DB      '         3 = Not ready, 4 = Not a CD-ROM', 0Dh, 0Ah, '$'

NoMSCDEXMsg:
DB      'Error: MSCDEX not installed', 0Dh, 0Ah, '$'

NotCDROMMsg:
DB      'Error: Not a CD-ROM drive', 0Dh, 0Ah, '$'

NotReadyMsg:
DB      'Error: Media not ready', 0Dh, 0Ah, '$'

ReadyMsg:
DB      'Media ready', 0Dh, 0Ah, '$'

DriveNum:
DB      0

DrivePath:
DB      '?:\*.*', 0

OldInt24:
DD      0
