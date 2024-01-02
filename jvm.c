#include "jvm.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "heap.h"
#include "read_class.h"

/** The name of the method to invoke to run the class file */
const char MAIN_METHOD[] = "main";
/**
 * The "descriptor" string for main(). The descriptor encodes main()'s signature,
 * i.e. main() takes a String[] and returns void.
 * If you're interested, the descriptor string is explained at
 * https://docs.oracle.com/javase/specs/jvms/se12/html/jvms-4.html#jvms-4.3.2.
 */
const char MAIN_DESCRIPTOR[] = "([Ljava/lang/String;)V";

/**
 * Represents the return value of a Java method: either void or an int or a reference.
 * For simplification, we represent a reference as an index into a heap-allocated array.
 * (In a real JVM, methods could also return object references or other primitives.)
 */
typedef struct {
    /** Whether this returned value is an int */
    bool has_value;
    /** The returned value (only valid if `has_value` is true) */
    int32_t value;
} optional_value_t;

/**
 * Runs a method's instructions until the method returns.
 *
 * @param method the method to run
 * @param locals the array of local variables, including the method parameters.
 *   Except for parameters, the locals are uninitialized.
 * @param class the class file the method belongs to
 * @param heap an array of heap-allocated pointers, useful for references
 * @return an optional int containing the method's return value
 */
optional_value_t execute(method_t *method, int32_t *locals, class_file_t *class,
                         heap_t *heap) {
    size_t pc = 0;
    int32_t *operand_stack = calloc(method->code.max_stack, sizeof(int32_t));
    int32_t stack_idx = 0;
    while (pc < method->code.code_length) {
        switch (method->code.code[pc]) {
            case i_bipush: {
                operand_stack[stack_idx] = (int32_t)((int8_t) method->code.code[pc + 1]);
                pc += 2;
                stack_idx += 1;
                break;
            }
            case i_iadd: {
                stack_idx -= 1;
                int32_t sum = operand_stack[stack_idx] + operand_stack[stack_idx - 1];
                operand_stack[stack_idx - 1] = sum;
                pc += 1;
                break;
            }
            case i_return: {
                optional_value_t result = {.has_value = false};
                free(operand_stack);
                return result;
            }
            case i_getstatic: {
                pc += 3;
                break;
            }
            case i_invokevirtual: {
                stack_idx -= 1;
                printf("%d\n", operand_stack[stack_idx]);
                pc += 3;
                break;
            }
            case i_iconst_m1 ... i_iconst_5: {
                operand_stack[stack_idx] = ((int32_t) method->code.code[pc]) - i_iconst_0;
                stack_idx += 1;
                pc += 1;
                break;
            }
            case i_sipush: {
                operand_stack[stack_idx] =
                    (short) (method->code.code[pc + 1] << 8 | method->code.code[pc + 2]);
                stack_idx += 1;
                pc += 3;
                break;
            }
            case i_isub: {
                stack_idx -= 1;
                operand_stack[stack_idx - 1] =
                    operand_stack[stack_idx - 1] - operand_stack[stack_idx];
                pc += 1;
                break;
            }
            case i_imul: {
                stack_idx -= 1;
                operand_stack[stack_idx - 1] =
                    operand_stack[stack_idx - 1] * operand_stack[stack_idx];
                pc += 1;
                break;
            }
            case i_idiv: {
                assert(operand_stack[stack_idx - 1] != 0);
                stack_idx -= 1;
                operand_stack[stack_idx - 1] =
                    operand_stack[stack_idx - 1] / operand_stack[stack_idx];
                pc += 1;
                break;
            }
            case i_irem: {
                assert(operand_stack[stack_idx - 1] != 0);
                stack_idx -= 1;
                operand_stack[stack_idx - 1] =
                    operand_stack[stack_idx - 1] % operand_stack[stack_idx];
                pc += 1;
                break;
            }
            case i_ineg: {
                operand_stack[stack_idx - 1] = -operand_stack[stack_idx - 1];
                pc += 1;
                break;
            }
            case i_ishl: {
                stack_idx -= 1;
                operand_stack[stack_idx - 1] = operand_stack[stack_idx - 1]
                                               << operand_stack[stack_idx];
                pc += 1;
                break;
            }
            case i_ishr: {
                stack_idx -= 1;
                operand_stack[stack_idx - 1] =
                    operand_stack[stack_idx - 1] >> operand_stack[stack_idx];
                pc += 1;
                break;
            }
            case i_iushr: {
                stack_idx -= 1;
                operand_stack[stack_idx - 1] =
                    ((uint32_t) operand_stack[stack_idx - 1]) >> operand_stack[stack_idx];
                pc += 1;
                break;
            }
            case i_iand: {
                stack_idx -= 1;
                operand_stack[stack_idx - 1] =
                    operand_stack[stack_idx - 1] & operand_stack[stack_idx];
                pc += 1;
                break;
            }
            case i_ior: {
                stack_idx -= 1;
                operand_stack[stack_idx - 1] =
                    operand_stack[stack_idx - 1] | operand_stack[stack_idx];
                pc += 1;
                break;
            }
            case i_ixor: {
                stack_idx -= 1;
                operand_stack[stack_idx - 1] =
                    operand_stack[stack_idx - 1] ^ operand_stack[stack_idx];
                pc += 1;
                break;
            }
            case i_iload: {
                operand_stack[stack_idx] = locals[method->code.code[pc + 1]];
                stack_idx += 1;
                pc += 2;
                break;
            }
            case i_iload_0 ... i_iload_3: {
                operand_stack[stack_idx] =
                    locals[(int32_t) method->code.code[pc] - i_iload_0];
                stack_idx += 1;
                pc += 1;
                break;
            }
            case i_istore: {
                stack_idx -= 1;
                locals[method->code.code[pc + 1]] = operand_stack[stack_idx];
                pc += 2;
                break;
            }
            case i_istore_0 ... i_istore_3: {
                stack_idx -= 1;
                locals[(int32_t) method->code.code[pc] - i_istore_0] =
                    operand_stack[stack_idx];
                pc += 1;
                break;
            }
            case i_iinc: {
                locals[method->code.code[pc + 1]] += (int8_t) method->code.code[pc + 2];
                pc += 3;
                break;
            }
            case i_ldc: {
                int32_t const_idx = (int32_t) method->code.code[pc + 1] - 1;
                operand_stack[stack_idx] =
                    ((CONSTANT_Integer_info *) class->constant_pool[const_idx].info)
                        ->bytes;
                stack_idx += 1;
                pc += 2;
                break;
            }
            case i_ifeq: {
                if (operand_stack[stack_idx - 1] == 0) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 1;
                break;
            }
            case i_ifne: {
                if (operand_stack[stack_idx - 1] != 0) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 1;
                break;
            }
            case i_iflt: {
                if (operand_stack[stack_idx - 1] < 0) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 1;
                break;
            }
            case i_ifge: {
                if (operand_stack[stack_idx - 1] >= 0) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 1;
                break;
            }
            case i_ifgt: {
                if (operand_stack[stack_idx - 1] > 0) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 1;
                break;
            }
            case i_ifle: {
                if (operand_stack[stack_idx - 1] <= 0) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 1;
                break;
            }
            case i_if_icmpeq: {
                if (operand_stack[stack_idx - 2] == operand_stack[stack_idx - 1]) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 2;
                break;
            }
            case i_if_icmpne: {
                if (operand_stack[stack_idx - 2] != operand_stack[stack_idx - 1]) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 2;
                break;
            }
            case i_if_icmplt: {
                if (operand_stack[stack_idx - 2] < operand_stack[stack_idx - 1]) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 2;
                break;
            }
            case i_if_icmpge: {
                if (operand_stack[stack_idx - 2] >= operand_stack[stack_idx - 1]) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 2;
                break;
            }
            case i_if_icmpgt: {
                if (operand_stack[stack_idx - 2] > operand_stack[stack_idx - 1]) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 2;
                break;
            }
            case i_if_icmple: {
                if (operand_stack[stack_idx - 2] <= operand_stack[stack_idx - 1]) {
                    int16_t b1 = method->code.code[pc + 1];
                    int8_t b2 = method->code.code[pc + 2];
                    pc += ((b1 << 8) | b2);
                }
                else {
                    pc += 3;
                }
                stack_idx -= 2;
                break;
            }
            case i_goto: {
                int16_t b1 = method->code.code[pc + 1];
                int8_t b2 = method->code.code[pc + 2];
                pc += ((b1 << 8) | b2);
                break;
            }
            case i_ireturn: {
                stack_idx -= 1;
                optional_value_t result = {.has_value = true,
                                           .value = operand_stack[stack_idx]};
                free(operand_stack);
                return result;
            }
            case i_invokestatic: {
                int16_t b1 = method->code.code[pc + 1];
                int8_t b2 = method->code.code[pc + 2];
                method_t *callee_method = find_method_from_index((b1 << 8) | b2, class);
                int16_t num_params = get_number_of_parameters(callee_method);
                int32_t *callee_locals =
                    calloc(sizeof(int32_t), callee_method->code.max_locals);
                for (int32_t i = num_params - 1; i >= 0; i--) {
                    stack_idx -= 1;
                    callee_locals[i] = operand_stack[stack_idx];
                }
                optional_value_t ret = execute(callee_method, callee_locals, class, heap);
                free(callee_locals);
                if (ret.has_value) {
                    operand_stack[stack_idx] = ret.value;
                    stack_idx += 1;
                }
                pc += 3;
                break;
            }
            case i_nop: {
                pc += 1;
                break;
            }
            case i_dup: {
                operand_stack[stack_idx] = operand_stack[stack_idx - 1];
                stack_idx += 1;
                pc += 1;
                break;
            }
            case i_newarray: {
                int32_t *array =
                    calloc(sizeof(int32_t), operand_stack[stack_idx - 1] + 1);
                array[0] = operand_stack[stack_idx - 1];
                for (int i = 1; i <= operand_stack[stack_idx - 1]; i++) {
                    array[i] = 0;
                }
                int32_t ref = heap_add(heap, array);
                operand_stack[stack_idx - 1] = ref;
                pc += 2;
                break;
            }
            case i_arraylength: {
                int32_t len = heap_get(heap, operand_stack[stack_idx - 1])[0];
                operand_stack[stack_idx - 1] = len;
                pc += 1;
                break;
            }
            case i_areturn: {
                stack_idx -= 1;
                optional_value_t result = {.has_value = true,
                                           .value = operand_stack[stack_idx]};
                free(operand_stack);
                return result;
            }
            case i_iastore: {
                heap_get(heap,
                         operand_stack[stack_idx - 3])[operand_stack[stack_idx - 2] + 1] =
                    operand_stack[stack_idx - 1];
                stack_idx -= 3;
                pc += 1;
                break;
            }
            case i_iaload: {
                stack_idx -= 1;
                operand_stack[stack_idx - 1] = heap_get(
                    heap, operand_stack[stack_idx - 1])[operand_stack[stack_idx] + 1];
                pc += 1;
                break;
            }
            case i_aload: {
                operand_stack[stack_idx] = locals[method->code.code[pc + 1]];
                stack_idx += 1;
                pc += 2;
                break;
            }
            case i_astore: {
                stack_idx -= 1;
                locals[method->code.code[pc + 1]] = operand_stack[stack_idx];
                pc += 2;
                break;
            }
            case i_aload_0 ... i_aload_3: {
                operand_stack[stack_idx] =
                    locals[(int32_t) method->code.code[pc] - i_aload_0];
                stack_idx += 1;
                pc += 1;
                break;
            }
            case i_astore_0 ... i_astore_3: {
                stack_idx -= 1;
                locals[(int32_t) method->code.code[pc] - i_astore_0] =
                    operand_stack[stack_idx];
                pc += 1;
                break;
            }
        }
    }
    free(operand_stack);

    // Return void
    optional_value_t result = {.has_value = false};
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "USAGE: %s <class file>\n", argv[0]);
        return 1;
    }

    // Open the class file for reading
    FILE *class_file = fopen(argv[1], "r");
    assert(class_file != NULL && "Failed to open file");

    // Parse the class file
    class_file_t *class = get_class(class_file);
    int error = fclose(class_file);
    assert(error == 0 && "Failed to close file");

    // The heap array is initially allocated to hold zero elements.
    heap_t *heap = heap_init();

    // Execute the main method
    method_t *main_method = find_method(MAIN_METHOD, MAIN_DESCRIPTOR, class);
    assert(main_method != NULL && "Missing main() method");
    /* In a real JVM, locals[0] would contain a reference to String[] args.
     * But since TeenyJVM doesn't support Objects, we leave it uninitialized. */
    int32_t locals[main_method->code.max_locals];
    // Initialize all local variables to 0
    memset(locals, 0, sizeof(locals));
    optional_value_t result = execute(main_method, locals, class, heap);
    assert(!result.has_value && "main() should return void");

    // Free the internal data structures
    free_class(class);

    // Free the heap
    heap_free(heap);
}
