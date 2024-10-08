#include "debug.h"
#include <stdio.h>

// Start of - To read m68k memory
#define MUL (7)
#define m68ki_cpu m68k

#include "shared.h"

#ifndef BUILD_TABLES
#include "m68ki_cycles.h"
#endif

#include "m68kconf.h"
#include "m68kcpu.h"
#include "m68kops.h"
// End of - To read m68k memory

// To read vram
#include "vdp_ctrl.h"

#include "../../sdl/sdl2/storage.h"
#include "../../sdl/sdl2/rom_analyzer.h"

#include "../../sdl/sdl2/dwarf.h"

// If set will execute one instruction and pause
int dbg_trace;
// If set will prevent instruction from executing and jump to main SDL loop
int dbg_paused;
// If set we are in the middle of the interrupt
int dbg_in_interrupt;
// If set will skip single breakpoint check
int dbg_continue_after_bp;
// Callback that is executed each time debug event happens (e.g. step)
typedef void (*debug_hook_t)(dbg_event_t type, void *data);
debug_hook_t debug_hooks[5];
int debug_hook_count = 0;

static breakpoint_t *first_bp = NULL;

breakpoint_t *add_bpt(hook_type_t type, unsigned int address, int width, int condition_provided, unsigned int value_equal)
{
    breakpoint_t *bp = (breakpoint_t *)malloc(sizeof(breakpoint_t));

    bp->type = type;
    bp->address = address;
    bp->width = width;
    bp->enabled = 1;

    bp->condition_provided = condition_provided;
    bp->value_equal = value_equal;

    if (first_bp)
    {
        bp->next = first_bp;
        bp->prev = first_bp->prev;
        first_bp->prev = bp;
        bp->prev->next = bp;
    }
    else
    {
        first_bp = bp;
        bp->next = bp;
        bp->prev = bp;
    }

    return bp;
}

static breakpoint_t *next_breakpoint(breakpoint_t *bp)
{
    return bp->next != first_bp ? bp->next : 0;
}

static void delete_breakpoint(breakpoint_t *bp)
{
    if (bp == first_bp)
    {
        if (bp->next == bp)
        {
            first_bp = NULL;
        }
        else
        {
            first_bp = bp->next;
        }
    }

    bp->next->prev = bp->prev;
    bp->prev->next = bp->next;

    free(bp);
}

void delete_breakpoint_with_address(unsigned int address)
{
    breakpoint_t *bp;
    for (bp = first_bp; bp; bp = next_breakpoint(bp))
    {
        if (bp->address == address) {
            delete_breakpoint(bp);
            break;
        }
    }
}

void clear_bpt_list()
{
    while (first_bp != NULL)
        delete_breakpoint(first_bp);
}

unsigned short cram_9b_to_16b(unsigned short data)
{
    /* Unpack 9-bit CRAM data (BBBGGGRRR) to 16-bit data (BBB0GGG0RRR0) */
    return (unsigned short)(((data & 0x1C0) << 3) | ((data & 0x038) << 2) | ((data & 0x007) << 1));
}

void read_string(char *str, unsigned int address)
{
    for (int i = 0; i < 256; i++)
    {
        str[i] = m68ki_read_8(address + i);
        if (str[i] == 0)
        {
            break;
        }
    }
}

// Send a notification of debug event to all subscribers
void debug_hook(dbg_event_t type, void *data)
{
    for (size_t i = 0; i < debug_hook_count; i++)
    {
        debug_hooks[i](type, data);
    }
}

void check_breakpoint(hook_type_t type, int width, unsigned int address, unsigned int value)
{
    if (type == HOOK_M68K_W)
    {
        // Prints debug messages from ROM to terminal
        // Send string pointer to this address within the ROM to display the message
        if (address == 0xa14400)
        {
            char log_message[256];
            read_string(log_message, value);
            printf("[ROM]: %s\n", log_message);
        }
    }

    breakpoint_t *bp;
    for (bp = first_bp; bp; bp = next_breakpoint(bp))
    {
        if (!(bp->type & type) || !bp->enabled)
            continue;

        if (type == HOOK_CRAM_W)
        {
            value = cram_9b_to_16b(value);

            // CRAM writes are always 2 bytes at the time
            // If we're monitoring odd address - compare second byte only
            if (bp->width == 1 && bp->address % 2)
            {
                value = value & 0xFF;
            }

            printf("cram write to address %u, width: %d, value: %u\n", address, width, value);
        }

        if (type == HOOK_M68K_W)
        {
            // printf("ram write to address %u, width: %d, value: %u\n", address, width, value);
            // RAM is located at 0xFF0000 - 0xFFFFFF
            // Address we're getting is full 32-bit address, so we need to mask it
            address = address & 0xFFFFFF;
        }

        if (bp->condition_provided && bp->value_equal != value)
            continue;

        if ((address <= (bp->address + bp->width)) && ((address + width) >= bp->address))
        {
            printf("breakpoint hit at addr: 0x%X, type: %u, width: %d, value: 0x%X\n", address, type, width, value);
            dbg_paused = 1;

            if (bp->once)
            {
                delete_breakpoint(bp);
            }
            break;
        }
    }
}

static struct fam *extracted_functions;
static struct timespec start, end;

static char *ym2612_buf;
static int send_next = 0;
const char *template = "{ \"type\": \"ym2612\", \"data\": [";

void process_breakpoints(hook_type_t type, int width, unsigned int address, unsigned int value)
{
    if (extracted_functions == NULL)
    {
        extracted_functions = get_functions();
    }

    switch (type)
    {
    case HOOK_Z80_W:
        // Collects and sends YM2612 note-on events to frontend for visualization
        visualize_ym2612(address, value);
        break;

    case HOOK_M68K_E:
        if (dbg_in_interrupt && !break_in_interrupt)
        {
            unsigned int pc = REG_PC;
            unsigned short opc = m68k_read_immediate_16(pc);

            if (opc != 0x4E73)
            { // rte
                break;
            }

            dbg_in_interrupt = 0; // we at rte
            break;
        }

        {
            unsigned short opc = m68k_read_immediate_16(m68k.prev_pc);
            int is_jsr = (opc >> 6) == 314;
            int is_jmp = (opc >> 6) == 315;
            if (is_jsr || is_jmp)
            {
                if (is_jsr && dbg_step_over)
                {
                    // Read return address from stack
                    unsigned int return_to = m68k_read_immediate_32(m68k.dar[M68K_REG_A7]);
                    breakpoint_t *bpt = add_bpt(HOOK_M68K_E, return_to, 1, 0, 0);
                    bpt->once = 1;
                    dbg_step_over = 0;
                    dbg_trace = 0;
                    dbg_paused = 0;
                }

                int function_found = 0;
                for (size_t i = 0; i < extracted_functions->len; i++)
                {
                    if (extracted_functions->arr[i] == m68k.pc)
                    {
                        function_found = 1;
                        break;
                    }
                }

                if (!function_found)
                {
                    printf("new function %04X, sent by %04X from %04X\n", m68k.pc, opc, m68k.prev_pc);
                    fam_append(extracted_functions, m68k.pc);
                    extract_functions(m68k.prev_pc, m68k.pc, read_memory);
                }
            }
        }

        if (!dbg_trace && !dbg_continue_after_bp)
        {
            // Will set dbg_paused if hit
            check_breakpoint(type, width, address, value);
        }

        if (dbg_step_over_line)
        {
            dwarf_ask_t *dwarf_info = dwarf_ask(m68k.pc);
            if (dwarf_info->line_number != dbg_step_over_line)
            {
                dbg_paused = 1;
                dbg_step_over_line = 0;
            }
        }

        if (dbg_paused)
        {
            dbg_continue_after_bp = 1;
            dbg_paused = 0;
            debug_hook(DBG_STEP, NULL);
            // Jump to main SDL loop
            longjmp(jmp_env, 1);
        }

        dbg_continue_after_bp = 0;

        if (dbg_trace)
        {
            dbg_trace = 0;
            dbg_paused = 1;
            break;
        }

        break;
    default:
        check_breakpoint(type, width, address, value);
        break;
    }
}

void visualize_ym2612(unsigned int address, unsigned int value)
{
    // Disabled for now, create a way to enable on demand
    return;

    if (address >= 0x4000 && address <= 0x4003)
    {
        if (ym2612_buf == NULL)
        {
            ym2612_buf = malloc(2000);
            sprintf(ym2612_buf, template);
        }

        uint64_t diff_in_ms = 0;

        // Capture time difference between previous key-on/key-off to track how much time has passed
        if (value == 0x28)
        {
            clock_gettime(CLOCK_MONOTONIC_RAW, &end);
            if (start.tv_sec != 0)
            {
                diff_in_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
            }
            clock_gettime(CLOCK_MONOTONIC_RAW, &start);
        }

        char *lastChar = send_next ? "]}" : ",";
        sprintf(ym2612_buf + strlen(ym2612_buf), "[%llu, %X, \"%X\"]%s", diff_in_ms, address & 3, value, lastChar);

        if (send_next)
        {
            send_next = 0;
            debug_hook(DBG_YM2612, ym2612_buf);
            sprintf(ym2612_buf, template);
        }

        // Send one write after key-on/key-off event OR when close to max capacity
        if (value == 0x28 || strlen(ym2612_buf) > 1900)
        {
            send_next = 1;
        }
    }
}

void set_debug_hook(void (*hook)(dbg_event_t type, void *data))
{
    debug_hooks[debug_hook_count++] = hook;
}

static unsigned char read_cram_byte(unsigned char *array, unsigned int addr)
{
    unsigned short pp = *(unsigned short *)&array[(addr >> 1) << 1];
    return cram_9b_to_16b(pp) >> ((addr & 1) ? 0 : 8);
}

unsigned char read_memory_byte(unsigned int address, char *type)
{
    if (type != NULL)
    {
        if (strcmp(type, "vram") == 0)
        {
            return READ_BYTE(vram, address);
        }
        else if (strcmp(type, "cram") == 0)
        {
            return read_cram_byte(cram, address);
        }
        else if (strcmp(type, "z80") == 0)
        {
            return zram[address - 0xA00000];
        }
    }

    return m68ki_read_8(address);
}

void write_memory_byte(unsigned int address, unsigned int value, char *memtype)
{
    if (memtype && strcmp(memtype, "vram") == 0) {
        vram[address] = value;
        return;
    }

    if (address >= 0xA04000 && address <= 0xA04003)
    {
        z80_memory_w(address - 0xA00000, value);
        return;
    }

    // We can't use m68ki_write_8 as it won't allow us to write to regions where CPU is not allowed to write (e.g. ROM)
    cpu_memory_map *temp = &m68k.memory_map[((address) >> 16) & 0xff];
    WRITE_BYTE(temp->base, (address) & 0xffff, value);
}

unsigned char *read_memory(unsigned int size, unsigned int address)
{
    unsigned char *bytes = malloc(size);
    for (unsigned int i = 0; i < size; i++)
    {
        bytes[i] = m68ki_read_8(address + i);
    }

    return bytes;
}
