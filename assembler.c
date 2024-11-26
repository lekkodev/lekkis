#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

/*
 
 Welcome to the C language version of the Lekko Assembler
 
 whether or not this *should* exist is debatable.
 
 This file describes a special purpose bytecode for lekkos.  It isn't meant for general purpose
 programming, it's made for small configuration functions.
 
 Op codes are one byte.  Should they be 64 bit for alignment issues? - probably?
 Arguments are 64 bits
 Binary values are [length, bytes], where length is a 16 bit value.
 
 All this can be pretty easily changed.
 
 Generally, sources are either Immediate or Argument
 
 Arguments are passed in with the request, Immediate values are right after the op code.
 
 D codes assume a 64 bit double
 I codes assume a 64 bit signed int
 
 As of now, we only assemble and run things with the same process.  That isn't a bad idea to keep up,
 since it makes it easy to do things like ensure that jumps stay within the program and stuff like that.
 
 State wise, it's a super basic stack machine that tends to overwrite the same value.  Probably it needs to be more complex,
 but most logic is of the form:
 
 if arg[x] cmp imediate:
   output 1
   return
 output 2
 return
 
 */

typedef enum {
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
    
    FUNCTION = 256, // Counters are one byte values, so we are using things at >255 for special values
    FUNCTION_COUNT,
} OpCode;

typedef struct {
    OpCode op_code;
    uint64_t arg1;
    uint16_t data_len;
    char *data;
} Line;

typedef struct {
    char *label;
    size_t offset;
} Label;

static const char *op_codes[] = {
    "noop", "pop", "jmp", "cjmp", "loadi", "loada",
    "iaddi", "iadda", "daddi", "dadda", "isubi", "isuba",
    "dsubi", "dsuba", "imuli", "imula", "dmuli", "dmula",
    "idivi", "idiva", "ddivi", "ddiva", "bequ", "sub",
    "out", "bout", "ret", "cget", "cset"
};

static const int op_codes_count = sizeof(op_codes) / sizeof(op_codes[0]);

OpCode find_op_code(const char *op_code) {
    for (uint16_t i = 0; i < op_codes_count; i++) {
        if (strcmp(op_codes[i], op_code) == 0) {
            return i;
        }
    }
    if (strcmp("function", op_code) == 0) {
        return 256;
    }
    return UINT16_MAX;
}

/*
    This is pretty shit.  It's in C just so that we can do everything in the same process.
 
    It assumes that input is valid.  There should be other work done to actual validate and sanitize the input.
 */
uint8_t* assemble(char *input, unsigned long input_length, size_t *output_size) {
    if (input[input_length] != '\0') {
        return NULL;
    }
    Label labels[1024];
    Line lines[4096];
    lines[0].op_code=FUNCTION_COUNT;
    size_t offset = 8;
    size_t function_count = 0;
    size_t label_count = 0;
    size_t lines_count = 0;
    size_t out_offset = 0;
    char *arg1;
    char *data;
    
    char *saveptr_line;
    char *line = strtok_r(input, "\n", &saveptr_line);
    while (line) {
        char *comment_start = strchr(line, ';'); // TODO this would mess up commas within strings..  Also.. we don't really need comments in the upload part
        if (comment_start) *comment_start = '\0';
        
        if (*line == '\t') {
            char *op_code_string = strtok(line, " \t");
            if (!op_code_string) {
                return NULL;
            }

            lines[++lines_count].op_code = find_op_code(op_code_string);
            switch (lines[lines_count].op_code) {
                case NOOP:
                case POP:
                case OUT:
                case RET:
                    offset += 1;
                    break;
                case JMP:
                case CJMP:
                case CALL:
                    offset += 9;
                    data = strtok(NULL, "\"");
                    lines[lines_count].data_len = strlen(data);
                    lines[lines_count].data = data;
                    break;
                case LOADI:
                case LOADA:
                case IADDI:
                case IADDA:
                case DADDI:
                case DADDA:
                case ISUBI:
                case ISUBA:
                case DSUBI:
                case DSUBA:
                case IMULI:
                case IMULA:
                case DMULI:
                case DMULA:
                case IDIVI:
                case IDIVA:
                case DDIVI:
                case DDIVA:
                    offset += 9;
                    arg1 = strtok(NULL, " \t");
                    if (!arg1) return NULL;
                    lines[lines_count].arg1 = strtoll(arg1, NULL, 10);
                    break;
                case BOUT:
                    data = strtok(NULL, "\"");
                    lines[lines_count].data_len = strlen(data);
                    lines[lines_count].data = data;
                    offset += 1 + 2 + lines[lines_count].data_len;
                    break;
                case BEQU:
                    arg1 = strtok(NULL, " \t");
                    if (!arg1) return NULL;
                    lines[lines_count].arg1 = strtoll(arg1, NULL, 10);
                    data = strtok(NULL, "\"");
                    lines[lines_count].data_len = strlen(data);
                    lines[lines_count].data = data;
                    offset += 1 + 8 + 2 + lines[lines_count].data_len;
                    break;
                case CGET:
                case CSET:
                    // TODO
                    break;
                case FUNCTION:
                    function_count++;
                    data = strtok(NULL, " \t;\n");
                    lines[lines_count].data_len = strlen(data);
                    lines[lines_count].data = data;
                    offset += 8 + 2 + lines[lines_count].data_len;
                    break;
                default:
                    return NULL;
            }
            
        } else if (*line != '\0') {
            char *label = strtok(line, ":");
            if (label) {
                labels[label_count].label = label;
                labels[label_count].offset = offset;
                label_count++;
            } else {
                return NULL;
            }
        }
        
        line = strtok_r(NULL, "\n", &saveptr_line);
    }
    
    *output_size = offset;
    /*
     This is the primary malloc for program code.  It should run once during assembly of a new version, and not other times.
     This should make it very simple to reason about memory lifetimes.
     */
    uint8_t *output = malloc(*output_size);
    if (!output) return NULL;
    
    for (size_t i = 0; i <= lines_count; i++) {
        switch (lines[i].op_code) {
            case NOOP:
            case POP:
            case OUT:
            case RET:
                output[out_offset++] = (uint8_t)(lines[i].op_code);
                break;
            case JMP:
            case CJMP:
            case CALL:
                output[out_offset++] = (uint8_t)(lines[i].op_code);
                for (size_t j = 0; j < label_count; j++) {
                    if (strcmp(lines[i].data, labels[j].label) == 0) {
                        memcpy(&output[out_offset], &(labels[j].offset), 8);
                        out_offset += 8;
                        goto FOUND_LABEL;
                    }
                }
                goto FAIL;
            case LOADI:
            case LOADA:
            case IADDI:
            case IADDA:
            case DADDI:
            case DADDA:
            case ISUBI:
            case ISUBA:
            case DSUBI:
            case DSUBA:
            case IMULI:
            case IMULA:
            case DMULI:
            case DMULA:
            case IDIVI:
            case IDIVA:
            case DDIVI:
            case DDIVA:
                output[out_offset++] = (uint8_t)(lines[i].op_code);
                memcpy(&output[out_offset], &(lines[i].arg1), 8);
                out_offset += 8;
                break;
            case BOUT:
                output[out_offset++] = (uint8_t)(lines[i].op_code);
                memcpy(&output[out_offset], &(lines[i].data_len), 2);
                out_offset += 2;
                memcpy(&output[out_offset], lines[i].data, lines[i].data_len);
                out_offset += lines[i].data_len;
                break;
            case BEQU:
                output[out_offset++] = (uint8_t)(lines[i].op_code);
                memcpy(&output[out_offset], &(lines[i].arg1), 8);
                out_offset += 8;
                memcpy(&output[out_offset], &(lines[i].data_len), 2);
                out_offset += 2;
                memcpy(&output[out_offset], lines[i].data, lines[i].data_len);
                out_offset += lines[i].data_len;
                break;
            case CGET:
            case CSET:
                // TODO
                break;
            case FUNCTION:
                for (size_t j = 0; j < label_count; j++) {
                    if (strcmp(lines[i].data, labels[j].label) == 0) {
                        memcpy(&output[out_offset], &(lines[i].data_len), 2);
                        out_offset += 2;
                        memcpy(&output[out_offset], lines[i].data, lines[i].data_len);
                        out_offset += lines[i].data_len;
                        memcpy(&output[out_offset], &(labels[j].offset), 8);
                        out_offset += 8;
                        goto FOUND_LABEL;
                    }
                }
                goto FAIL;
            case FUNCTION_COUNT:
                memcpy(&output[out_offset], &function_count, 8);
                out_offset += 8;
                break;
            default:
                goto FAIL;
        }
    FOUND_LABEL:
        continue;
    }
    
    return output;
FAIL:
    free(output);
    return NULL; // TODO - actually return error codes to return to the user (and hook this up to a different port or something)
}


/*

int main() {
    const char *test_input =
        "; Sample Lekko Assembly file\n"
        "\n"
        "\tfunction foo\n"
        "\n"
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
    size_t output_size = 0;

    uint8_t *output = assemble(input, strlen(input), &output_size);
    if (!output) {
        fprintf(stderr, "Assembly failed.\n");
        free(input);
        return 1;
    }

    printf("Assembled binary (%zu bytes):\n", output_size);
    for (size_t i = 0; i < output_size; i++) {
        printf("%02x ", output[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    free(output);
    free(input);
    return 0;
}

*/
