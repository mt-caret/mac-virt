/* Objective-C glue to Apple's Virtualization framework.
   Compiled as Objective-C with ARC; see lib/dune for the -x/-fobjc-arc flags
   and the -framework link flags. */

#import <Foundation/Foundation.h>
#import <Virtualization/Virtualization.h>

#define CAML_NAME_SPACE
#include <caml/alloc.h>
#include <caml/memory.h>
#include <caml/mlvalues.h>

/* Whether this host (hardware + macOS version) can run virtual machines. */
CAMLprim value vz_is_supported(value unit)
{
  CAMLparam1(unit);
  CAMLreturn(Val_bool([VZVirtualMachine isSupported]));
}

/* Build a trivial configuration with [cpu_count] CPUs and [memory_size] bytes
   of RAM, validate it, and return [None] on success or [Some message]. */
CAMLprim value vz_validate_basic_config(value v_cpu, value v_mem)
{
  CAMLparam2(v_cpu, v_mem);
  CAMLlocal2(result, msg);

  @autoreleasepool {
    VZVirtualMachineConfiguration *config =
      [[VZVirtualMachineConfiguration alloc] init];
    config.CPUCount = Long_val(v_cpu);
    config.memorySize = Int64_val(v_mem);

    NSError *error = nil;
    if ([config validateWithError:&error]) {
      result = Val_int(0); /* None */
    } else {
      const char *desc = error.localizedDescription.UTF8String;
      msg = caml_copy_string(desc ? desc : "unknown validation error");
      result = caml_alloc_small(1, 0); /* Some */
      Field(result, 0) = msg;
    }
  }

  CAMLreturn(result);
}
