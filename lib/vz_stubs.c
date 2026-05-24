/* Objective-C glue to Apple's Virtualization framework.
   Compiled as Objective-C with ARC; see lib/dune for the -x/-fobjc-arc flags
   and the -framework link flags. */

#import <Foundation/Foundation.h>
#import <Virtualization/Virtualization.h>

#define CAML_NAME_SPACE
#include <caml/alloc.h>
#include <caml/custom.h>
#include <caml/memory.h>
#include <caml/mlvalues.h>

/* ---- Wrapping Objective-C objects in OCaml custom blocks ---- */

/* A custom block holding one retained Objective-C object; the finalizer
   releases it. */
static void vz_object_finalize(value v)
{
  void *ptr = *((void **)Data_custom_val(v));
  if (ptr != NULL) CFBridgingRelease(ptr);
}

static struct custom_operations vz_object_ops = {
  "io.mac_virt.object",
  vz_object_finalize,
  custom_compare_default,
  custom_hash_default,
  custom_serialize_default,
  custom_deserialize_default,
  custom_compare_ext_default,
  custom_fixed_length_default,
};

/* Wrap [obj], taking ownership of one reference. */
static value vz_alloc(id obj)
{
  value v = caml_alloc_custom(&vz_object_ops, sizeof(void *), 0, 1);
  *((void **)Data_custom_val(v)) = (void *)CFBridgingRetain(obj);
  return v;
}

static id vz_unwrap(value v) { return (__bridge id)(*((void **)Data_custom_val(v))); }

/* A VZVirtualMachine together with the serial queue it is confined to: every
   operation on the machine must run on that queue. */
typedef struct {
  void *vm;
  void *queue;
} vz_vm;

static void vz_vm_finalize(value v)
{
  vz_vm *p = Data_custom_val(v);
  if (p->vm != NULL) CFBridgingRelease(p->vm);
  if (p->queue != NULL) CFBridgingRelease(p->queue);
}

static struct custom_operations vz_vm_ops = {
  "io.mac_virt.virtual_machine",
  vz_vm_finalize,
  custom_compare_default,
  custom_hash_default,
  custom_serialize_default,
  custom_deserialize_default,
  custom_compare_ext_default,
  custom_fixed_length_default,
};

/* ---- Top level ---- */

CAMLprim value vz_is_supported(value unit)
{
  CAMLparam1(unit);
  CAMLreturn(Val_bool([VZVirtualMachine isSupported]));
}

/* ---- Configuration ---- */

CAMLprim value vz_configuration_create(value v_cpu, value v_mem)
{
  CAMLparam2(v_cpu, v_mem);
  CAMLlocal1(result);
  @autoreleasepool {
    VZVirtualMachineConfiguration *config = [[VZVirtualMachineConfiguration alloc] init];
    config.CPUCount = Long_val(v_cpu);
    config.memorySize = Int64_val(v_mem);
    result = vz_alloc(config);
  }
  CAMLreturn(result);
}

CAMLprim value vz_configuration_cpu_count(value v_config)
{
  CAMLparam1(v_config);
  VZVirtualMachineConfiguration *config = vz_unwrap(v_config);
  CAMLreturn(Val_long(config.CPUCount));
}

CAMLprim value vz_configuration_memory_size(value v_config)
{
  CAMLparam1(v_config);
  VZVirtualMachineConfiguration *config = vz_unwrap(v_config);
  CAMLreturn(caml_copy_int64(config.memorySize));
}

CAMLprim value vz_configuration_validate(value v_config)
{
  CAMLparam1(v_config);
  CAMLlocal2(result, msg);
  @autoreleasepool {
    VZVirtualMachineConfiguration *config = vz_unwrap(v_config);
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

/* ---- Virtual machine ---- */

/* Precondition: [v_config] has already validated successfully. Creating a
   machine from an invalid configuration raises an Objective-C exception. */
CAMLprim value vz_virtual_machine_create(value v_config)
{
  CAMLparam1(v_config);
  CAMLlocal1(result);
  @autoreleasepool {
    VZVirtualMachineConfiguration *config = vz_unwrap(v_config);
    dispatch_queue_t queue = dispatch_queue_create("io.mac_virt.vm", DISPATCH_QUEUE_SERIAL);
    __block VZVirtualMachine *vm = nil;
    dispatch_sync(queue, ^{
      vm = [[VZVirtualMachine alloc] initWithConfiguration:config queue:queue];
    });
    result = caml_alloc_custom(&vz_vm_ops, sizeof(vz_vm), 0, 1);
    vz_vm *p = Data_custom_val(result);
    p->vm = (void *)CFBridgingRetain(vm);
    p->queue = (void *)CFBridgingRetain(queue);
  }
  CAMLreturn(result);
}

CAMLprim value vz_virtual_machine_state(value v_vm)
{
  CAMLparam1(v_vm);
  vz_vm *p = Data_custom_val(v_vm);
  VZVirtualMachine *vm = (__bridge VZVirtualMachine *)p->vm;
  dispatch_queue_t queue = (__bridge dispatch_queue_t)p->queue;
  __block VZVirtualMachineState state = VZVirtualMachineStateStopped;
  dispatch_sync(queue, ^{
    state = vm.state;
  });
  CAMLreturn(Val_long((long)state));
}
