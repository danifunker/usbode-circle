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

org     0x100           ; COM file format

;==============================================================================
; CONSTANTS
;==============================================================================

; MSCDEX API functions (INT 2Fh)
MSCDEX_CHECK            equ     0x1500  ; Installation check
MSCDEX_IS_CDROM         equ     0x150B  ; Check if drive is CD-ROM
MSCDEX_CDROM_MAGIC      equ     0xADAD  ; Magic value returned for CD-ROM

; DOS API functions (INT 21h)
DOS_PRINT_STRING        equ     0x09    ; Print $-terminated string
DOS_GET_INT_VECTOR      equ     0x35    ; Get interrupt vector
DOS_SET_INT_VECTOR      equ     0x25    ; Set interrupt vector
DOS_FIND_FIRST          equ     0x4E    ; Find first file
DOS_TERMINATE           equ     0x4C    ; Terminate with exit code

; DOS interrupt vectors
INT_DOS                 equ     0x21    ; DOS services
INT_MULTIPLEX           equ     0x2F    ; Multiplex interrupt
INT_CRIT_ERROR          equ     0x24    ; Critical error handler

; Exit codes
EXIT_READY              equ     0       ; Media ready
EXIT_USAGE              equ     1       ; Bad usage
EXIT_NO_MSCDEX          equ     2       ; MSCDEX not installed
EXIT_NOT_READY          equ     3       ; Media not ready
EXIT_NOT_CDROM          equ     4       ; Not a CD-ROM drive

; File attributes
ATTR_DIRECTORY          equ     0x10    ; Directory attribute

; Character constants
CHAR_SPACE              equ     ' '     ; Space character
CHAR_TAB                equ     0x09    ; Tab character
CHAR_CR                 equ     0x0D    ; Carriage return
CHAR_LF                 equ     0x0A    ; Line feed
CHAR_DOLLAR             equ     '$'     ; String terminator for DOS

; Conversion masks
MASK_UPPERCASE          equ     0xDF    ; AND mask to convert to uppercase

; Drive constants
MAX_DRIVE               equ     25      ; Maximum drive number (Z = 25)

; Critical error handler codes
CRIT_ERR_FAIL           equ     3       ; Fail the operation

;==============================================================================
; MAIN CODE
;==============================================================================

; Parse command line to get drive letter
        mov     si, 0x80        ; Point to command line length
        mov     cl, [si]        ; Get length
        xor     ch, ch          ; CX = command line length
        jcxz    empty_cmdline   ; If empty, show usage
        jmp     parse_drive     ; Skip jump stub

empty_cmdline:
        jmp     usage           ; Jump to usage (out of short range)

parse_drive:
        inc     si              ; Skip length byte

        ; Skip leading whitespace (spaces and tabs)
skip_ws:
        lodsb                   ; AL = next char, SI++
        loop    skip_ws_check   ; Decrement CX and check
        jmp     show_usage      ; Ran out of chars

skip_ws_check:
        cmp     al, CHAR_SPACE  ; Space?
        je      skip_ws
        cmp     al, CHAR_TAB    ; Tab?
        je      skip_ws
        cmp     al, CHAR_CR     ; Carriage return?
        je      show_usage      ; End of line with no argument

        ; AL now contains the drive letter
        and     al, MASK_UPPERCASE ; Convert to uppercase
        sub     al, 'A'         ; Convert to drive number
        jc      show_usage      ; Invalid if < 'A'
        cmp     al, MAX_DRIVE   ; Check if > 'Z'
        ja      show_usage

        mov     [drive_num], al ; Save drive number

        ; Check if MSCDEX is installed
        mov     ax, MSCDEX_CHECK ; MSCDEX installation check
        xor     bx, bx          ; Clear BX
        int     INT_MULTIPLEX
        cmp     bx, 0           ; BX = 0 means not installed
        je      no_mscdex       ; MSCDEX not installed

        ; Check if this drive is a CD-ROM drive
        mov     ax, MSCDEX_IS_CDROM ; Check if drive is CD-ROM
        mov     cl, [drive_num] ; CL = drive number (0-based)
        int     INT_MULTIPLEX
        cmp     bx, MSCDEX_CDROM_MAGIC ; Magic return value if CD-ROM
        jne     not_cdrom       ; Not a CD-ROM drive
        cmp     ax, 0           ; AX != 0 means it's a CD-ROM
        je      not_cdrom

        ; Save old critical error handler
        mov     ax, (DOS_GET_INT_VECTOR << 8) | INT_CRIT_ERROR
        int     INT_DOS
        mov     [old_int24], bx
        mov     [old_int24+2], es

        ; Install our critical error handler
        mov     ax, (DOS_SET_INT_VECTOR << 8) | INT_CRIT_ERROR
        mov     dx, crit_err_handler
        int     INT_DOS

        ; Build path string with actual drive letter
        mov     al, [drive_num]
        add     al, 'A'         ; Convert to letter
        mov     [drive_path], al ; Set drive letter

        ; Try to access root directory via DOS
        mov     ax, DOS_FIND_FIRST << 8 ; Find first file
        mov     cx, ATTR_DIRECTORY ; Directory attribute
        mov     dx, drive_path  ; Point to constructed path
        int     INT_DOS
        pushf                   ; Save flags (carry indicates error)

        ; Restore old critical error handler
        push    ds
        mov     ax, (DOS_SET_INT_VECTOR << 8) | INT_CRIT_ERROR
        lds     dx, [old_int24]
        int     INT_DOS
        pop     ds

        popf                    ; Restore flags
        jc      not_ready       ; If error, drive not ready

ready:
        mov     dx, ready_msg
        mov     ah, DOS_PRINT_STRING
        int     INT_DOS
        mov     al, EXIT_READY
        jmp     exit

no_mscdex:
        mov     dx, no_mscdex_msg
        mov     ah, DOS_PRINT_STRING
        int     INT_DOS
        mov     al, EXIT_NO_MSCDEX
        jmp     exit

not_ready:
        mov     dx, not_ready_msg
        mov     ah, DOS_PRINT_STRING
        int     INT_DOS
        mov     al, EXIT_NOT_READY
        jmp     exit

not_cdrom:
        mov     dx, not_cdrom_msg
        mov     ah, DOS_PRINT_STRING
        int     INT_DOS
        mov     al, EXIT_NOT_CDROM
        jmp     exit

; Jump stub for out-of-range conditional jumps
show_usage:
        jmp     usage

usage:
        mov     dx, usage_msg
        mov     ah, DOS_PRINT_STRING
        int     INT_DOS
        mov     al, EXIT_USAGE

exit:
        mov     ah, DOS_TERMINATE
        int     INT_DOS

;==============================================================================
; INTERRUPT HANDLERS
;==============================================================================

; Critical error handler - always return fail
crit_err_handler:
        mov     al, CRIT_ERR_FAIL ; Fail
        iret

;==============================================================================
; DATA SECTION
;==============================================================================

usage_msg:
        db      'Usage: cdready <drive letter>', CHAR_CR, CHAR_LF
        db      'Returns: 0 = Ready, 1 = Bad usage, 2 = No MSCDEX,', CHAR_CR, CHAR_LF
        db      '         3 = Not ready, 4 = Not a CD-ROM', CHAR_CR, CHAR_LF, CHAR_DOLLAR

no_mscdex_msg:
        db      'Error: MSCDEX not installed', CHAR_CR, CHAR_LF, CHAR_DOLLAR

not_cdrom_msg:
        db      'Error: Not a CD-ROM drive', CHAR_CR, CHAR_LF, CHAR_DOLLAR

not_ready_msg:
        db      'Error: Media not ready', CHAR_CR, CHAR_LF, CHAR_DOLLAR

ready_msg:
        db      'Media ready', CHAR_CR, CHAR_LF, CHAR_DOLLAR

drive_num:
        db      0

drive_path:
        db      '?:\*.*', 0

old_int24:
        dd      0
