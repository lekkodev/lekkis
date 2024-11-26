//
//  vm.c
//  Lekkis
//
//  Created by jonathan on 11/18/24.
//

#include "vm.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assembler.h"

typedef uint8_t Opcode_t;
typedef enum OpCode_t {
    NOOP,
    POP,
    JMP,
    CJMP,
    LOADI,
    LOADA,
    IADDI,
    IADDA,
    DADDI,
    DADDA,
    ISUBI,
    ISUBA,
    DSUBI,
    DSUBA,
    IMULI,
    IMULA,
    DMULI,
    DMULA,
    IDIVI,
    IDIVA,
    DDIVI,
    DDIVA,
    BEQU,
    CALL,
    OUT,
    BOUT,
    RET,
    CGET,
    CSET,
} OpCode;

typedef struct {
    uint64_t *arguments;
    uint8_t *program;
    uint64_t *stack;
    uint8_t *output;
} Runnable;

typedef struct {
    uint16_t length;
    uint8_t data[];
} LV;

#define DISPATCH() do { goto *dispatch_table[*program_ptr++]; } while (0)

uint8_t * runProgram(Runnable *r, uint8_t *program_ptr, size_t *output_size) {
    uint64_t *stack_ptr = r->stack;
    uint8_t *output_ptr = r->output;

    void* dispatch_table[] = {
        &&NOOP,
        &&POP,
        &&JMP,
        &&CJMP,
        &&LOADI,
        &&LOADA,
        &&IADDI,
        &&IADDA,
        &&DADDI,
        &&DADDA,
        &&ISUBI,
        &&ISUBA,
        &&DSUBI,
        &&DSUBA,
        &&IMULI,
        &&IMULA,
        &&DMULI,
        &&DMULA,
        &&IDIVI,
        &&IDIVA,
        &&DDIVI,
        &&DDIVA,
        &&BEQU,
        &&CALL,
        &&OUT,
        &&BOUT,
        &&RET,
        &&CGET,
        &&CSET,
    };

    DISPATCH();

NOOP:
    DISPATCH();

POP:
    stack_ptr--;
    DISPATCH();

JMP:
    program_ptr = r->program + *program_ptr;
    DISPATCH();

CJMP:
    if (*stack_ptr == 0) {
        program_ptr = r->program + *program_ptr;
    } else {
        program_ptr++;
    }
    DISPATCH();

LOADI:
    *(++stack_ptr) = *(uint64_t*)program_ptr;
    program_ptr += sizeof(uint64_t);
    DISPATCH();

LOADA:
    *(++stack_ptr) = r->arguments[*(uint64_t*)program_ptr];
    program_ptr += sizeof(uint64_t);
    DISPATCH();

IADDI:
    *stack_ptr += *(uint64_t*)program_ptr;
    program_ptr += sizeof(uint64_t);
    DISPATCH();

IADDA:
    *stack_ptr += r->arguments[*(uint64_t*)program_ptr];
    program_ptr += sizeof(uint64_t);
    DISPATCH();

DADDI:
    *stack_ptr = (double)*stack_ptr + (double)(*(uint64_t*)program_ptr);
    program_ptr += sizeof(uint64_t);
    DISPATCH();

DADDA:
    *stack_ptr = (double)*stack_ptr + (double)r->arguments[*(uint64_t*)program_ptr];
    program_ptr += sizeof(uint64_t);
    DISPATCH();

ISUBI:
    *stack_ptr -= *(uint64_t*)program_ptr;
    program_ptr += sizeof(uint64_t);
    DISPATCH();

ISUBA:
    *stack_ptr -= r->arguments[*(uint64_t*)program_ptr];
    program_ptr += sizeof(uint64_t);
    DISPATCH();

DSUBI:
    *stack_ptr = (double)*stack_ptr - (double)(*(uint64_t*)program_ptr);
    program_ptr += sizeof(uint64_t);
    DISPATCH();

DSUBA:
    *stack_ptr = (double)*stack_ptr - (double)r->arguments[*(uint64_t*)program_ptr];
    program_ptr += sizeof(uint64_t);
    DISPATCH();

IMULI:
    *stack_ptr *= *(uint64_t*)program_ptr;
    program_ptr += sizeof(uint64_t);
    DISPATCH();

IMULA:
    *stack_ptr *= r->arguments[*(uint64_t*)program_ptr];
    program_ptr += sizeof(uint64_t);
    DISPATCH();

DMULI:
    *stack_ptr = (double)*stack_ptr * (double)(*(uint64_t*)program_ptr);
    program_ptr += sizeof(uint64_t);
    DISPATCH();

DMULA:
    *stack_ptr = (double)*stack_ptr * (double)r->arguments[*(uint64_t*)program_ptr];
    program_ptr += sizeof(uint64_t);
    DISPATCH();

IDIVI:
    *stack_ptr /= *(uint64_t*)program_ptr;
    program_ptr += sizeof(uint64_t);
    DISPATCH();

IDIVA:
    *stack_ptr /= r->arguments[*(uint64_t*)program_ptr];
    program_ptr += sizeof(uint64_t);
    DISPATCH();

DDIVI:
    *stack_ptr = (double)*stack_ptr / (double)(*(uint64_t*)program_ptr);
    program_ptr += sizeof(uint64_t);
    DISPATCH();

DDIVA:
    *stack_ptr = (double)*stack_ptr / (double)r->arguments[*(uint64_t*)program_ptr];
    program_ptr += sizeof(uint64_t);
    DISPATCH();
BEQU:
    {
        LV *left = (LV*)&r->arguments[r->arguments[*(uint64_t*)program_ptr]];
        program_ptr += sizeof(uint64_t);
        LV *right = (LV*)program_ptr;
        program_ptr += sizeof(uint16_t) + right->length;
        *stack_ptr = (left->length == right->length) ? memcmp(left->data, right->data, left->length) : 1;
    }
    DISPATCH();
CALL:
    output_ptr = runProgram(&(Runnable){
        .arguments = r->arguments,
        .program = r->program,
        .stack = stack_ptr,
    }, program_ptr + *(int64_t*)(program_ptr), NULL);
    DISPATCH();

OUT:
    *output_ptr = *stack_ptr;
    output_ptr += sizeof(uint64_t);
    DISPATCH();

BOUT:
    memcpy(output_ptr, program_ptr, ((LV*)program_ptr)->length+2);
    output_ptr += ((LV*)program_ptr)->length+2;
    program_ptr += ((LV*)program_ptr)->length+2;
    DISPATCH();
RET:
    if (output_size) *output_size = output_ptr - r->output;
    return output_ptr;
CGET:
CSET:
    // TODO Implement
    // counter get and counter set
    // simplest would be to have a hash table for each program, or have programs declare their hash space needs.
    DISPATCH();
}

void* function_ptr(uint8_t* program, LV *function) {
    uint64_t num_functions = *(uint64_t*)program;
    uint8_t *ptr = program + 8;
    LV *listing = (LV *)ptr;
    uint64_t i = 0;
    
    while (i < num_functions) {
        ptr += 2 + listing->length;
        if (listing->length == function->length && memcmp(listing->data, function->data, listing->length) == 0) {
            return program + *ptr;
        }
    }
    return NULL;
}


int main_vm(void) {
    /*  This is why I made an assembler!!!
    uint8_t program[] = {
            LOADA, 0x03,                        // Load argument[3] onto the stack
            BEQU, 0x03, 0x04, 0x00, 'M', 'e', 'e', 'p', // Check if arg[3] == LV{4, "Meep"}
            CJMP, 0x19,                         // Conditional jump to output 4 if match
            LOADA, 0x02,                        // Load arg[2] (integer) onto stack
            IMULI, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Multiply arg[2] by 7
            OUT,
            RET,
            LOADI, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            BOUT, 0x02, 0x00, 'F', 'u',
            RET,                                // End program
        };
     */
    
    const char *test_input =
   //     "; Sample Lekko Assembly file\n"
   //     "\n"
        "\tfunction foo\n"
   //     "\n"
        "foo:\n"
        "\tbequ 3 \"Meep\"\n"
        "\tcjmp FOUR\n"
        "\tloada 2\n"
        "\timuli 7\n"
        "\tout\n"
        "\tret\n"
        "FOUR:\n"
        "\tloadi 4\n"
        "\tbout \"Fu\"\n"
        "\tret\n";

    char *input = strdup(test_input);
    size_t program_size = 0;

    uint8_t *program = assemble(input, strlen(input), &program_size);

    uint8_t func_name[] = {0x03, 0x00, 'f', 'o', 'o'};
    void *ptr = function_ptr(program, func_name);
    printf("ptr = %" PRIu64 "\n", ptr);
    
        // Arguments with arg[3] as LV{4, "Meep"}
        uint8_t arguments[] = { // types are all wrong - mixing in the 64 bit and buffers is wrong
            0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x04, 0x00, 'M', 'e', 'e', 'p' // arg[3] as LV{4, "Meep"}
        };
    uint64_t stack[64] = {0};           // Stack space for execution
    uint8_t output[4096] = {0};           // Output buffer for results

    Runnable r = {
        .arguments = arguments,
        .program = program,
        .stack = stack,
        .output = output
    };

    // Run the program
    runProgram(&r, ptr, NULL);

    // Print the stack after execution
    printf("Stack after execution:\n");
    for (int i = 0; i < 64; i++) {
        if (r.stack[i] != 0) {
            printf("stack[%d] = %" PRIu64 "\n", i, r.stack[i]);
        }
    }

    // Print the output after execution
    printf("\nOutput after execution:\n");
    for (int i = 0; i < 8; i++) {
        printf("output[%d] = %d\n", i, output[i]);
    }

    return 0;
}

void runFunction(uint8_t *program, uint8_t *output, size_t *output_size, uint8_t *arguments, uint8_t* function_name) {
    void *ptr = function_ptr(program, function_name);
    if (ptr) {
        uint64_t stack[1024] = {0};
        Runnable r = { .arguments = (uint64_t *)arguments, .program = program, .stack = stack, .output = output };
        runProgram(&r, ptr, output_size);
    }
}
