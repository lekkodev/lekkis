//
//  vm.h
//  Lekkis
//
//  Created by jonathan on 11/18/24.
//

#ifndef vm_h
#define vm_h

#include <stdio.h>

void runFunction(uint8_t *program, uint8_t *output, size_t *output_size, uint8_t *arguments, uint8_t* function_name);

#endif /* vm_h */