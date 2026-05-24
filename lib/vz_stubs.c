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

/* ---- Small value constructors ---- */

/* A boxed block with the given [tag] and single field [v]; used to build
   [Some v] / [Ok v] (tag 0) and [Error v] (tag 1). */
static value vz_block1(int tag, value v)
{
  CAMLparam1(v);
  CAMLlocal1(r);
  r = caml_alloc_small(1, tag);
  Field(r, 0) = v;
  CAMLreturn(r);
}

static value vz_error_of_nserror(NSError *error, const char *fallback)
{
  const char *desc = error.localizedDescription.UTF8String;
  return vz_block1(1, caml_copy_string(desc != NULL ? desc : fallback));
}

/* ---- NSData <-> OCaml string ---- */

static value vz_string_of_data(NSData *data)
{
  return caml_alloc_initialized_string(data.length, data.bytes);
}

static NSData *vz_data_of_string(value s)
{
  return [NSData dataWithBytes:String_val(s) length:caml_string_length(s)];
}

static NSString *vz_nsstring(value s)
{
  return [NSString stringWithUTF8String:String_val(s)];
}

/* ---- Top level ---- */

CAMLprim value vz_is_supported(value unit)
{
  CAMLparam1(unit);
  CAMLreturn(Val_bool([VZVirtualMachine isSupported]));
}

/* ---- Hardware model ---- */

CAMLprim value vz_hardware_model_of_data(value v_data)
{
  CAMLparam1(v_data);
  CAMLlocal1(result);
  @autoreleasepool {
    VZMacHardwareModel *model =
      [[VZMacHardwareModel alloc] initWithDataRepresentation:vz_data_of_string(v_data)];
    result = (model == nil) ? Val_int(0) : vz_block1(0, vz_alloc(model));
  }
  CAMLreturn(result);
}

CAMLprim value vz_hardware_model_to_data(value v_model)
{
  CAMLparam1(v_model);
  CAMLlocal1(result);
  @autoreleasepool {
    VZMacHardwareModel *model = vz_unwrap(v_model);
    result = vz_string_of_data(model.dataRepresentation);
  }
  CAMLreturn(result);
}

CAMLprim value vz_hardware_model_is_supported(value v_model)
{
  CAMLparam1(v_model);
  VZMacHardwareModel *model = vz_unwrap(v_model);
  CAMLreturn(Val_bool(model.supported));
}

/* ---- Machine identifier ---- */

CAMLprim value vz_machine_identifier_create(value unit)
{
  CAMLparam1(unit);
  CAMLlocal1(result);
  @autoreleasepool {
    result = vz_alloc([[VZMacMachineIdentifier alloc] init]);
  }
  CAMLreturn(result);
}

CAMLprim value vz_machine_identifier_of_data(value v_data)
{
  CAMLparam1(v_data);
  CAMLlocal1(result);
  @autoreleasepool {
    VZMacMachineIdentifier *id =
      [[VZMacMachineIdentifier alloc] initWithDataRepresentation:vz_data_of_string(v_data)];
    result = (id == nil) ? Val_int(0) : vz_block1(0, vz_alloc(id));
  }
  CAMLreturn(result);
}

CAMLprim value vz_machine_identifier_to_data(value v_id)
{
  CAMLparam1(v_id);
  CAMLlocal1(result);
  @autoreleasepool {
    VZMacMachineIdentifier *id = vz_unwrap(v_id);
    result = vz_string_of_data(id.dataRepresentation);
  }
  CAMLreturn(result);
}

/* ---- Auxiliary storage ---- */

CAMLprim value vz_auxiliary_storage_create(value v_path, value v_model)
{
  CAMLparam2(v_path, v_model);
  CAMLlocal1(result);
  @autoreleasepool {
    NSError *error = nil;
    VZMacAuxiliaryStorage *storage =
      [[VZMacAuxiliaryStorage alloc] initCreatingStorageAtURL:[NSURL fileURLWithPath:vz_nsstring(v_path)]
                                                hardwareModel:vz_unwrap(v_model)
                                                      options:0
                                                        error:&error];
    result = (storage == nil)
               ? vz_error_of_nserror(error, "failed to create auxiliary storage")
               : vz_block1(0, vz_alloc(storage));
  }
  CAMLreturn(result);
}

/* ---- Mac platform ---- */

CAMLprim value vz_mac_platform_create(value v_model, value v_id, value v_storage)
{
  CAMLparam3(v_model, v_id, v_storage);
  CAMLlocal1(result);
  @autoreleasepool {
    VZMacPlatformConfiguration *platform = [[VZMacPlatformConfiguration alloc] init];
    platform.hardwareModel = vz_unwrap(v_model);
    platform.machineIdentifier = vz_unwrap(v_id);
    platform.auxiliaryStorage = vz_unwrap(v_storage);
    result = vz_alloc(platform);
  }
  CAMLreturn(result);
}

/* ---- Restore image ---- */

/* Block until [block] (an asynchronous call taking a (image, error) completion
   handler) finishes, then return [Ok image] / [Error message]. */
static value vz_await_restore_image(void (^start)(void (^)(VZMacOSRestoreImage *, NSError *)))
{
  __block VZMacOSRestoreImage *image = nil;
  __block NSError *failure = nil;
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  start(^(VZMacOSRestoreImage *img, NSError *err) {
    image = img;
    failure = err;
    dispatch_semaphore_signal(done);
  });
  dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
  return (image == nil) ? vz_error_of_nserror(failure, "failed to load restore image")
                        : vz_block1(0, vz_alloc(image));
}

CAMLprim value vz_restore_image_fetch_latest(value unit)
{
  CAMLparam1(unit);
  CAMLlocal1(result);
  @autoreleasepool {
    result = vz_await_restore_image(^(void (^handler)(VZMacOSRestoreImage *, NSError *)) {
      [VZMacOSRestoreImage fetchLatestSupportedWithCompletionHandler:handler];
    });
  }
  CAMLreturn(result);
}

CAMLprim value vz_restore_image_load(value v_path)
{
  CAMLparam1(v_path);
  CAMLlocal1(result);
  @autoreleasepool {
    NSURL *url = [NSURL fileURLWithPath:vz_nsstring(v_path)];
    result = vz_await_restore_image(^(void (^handler)(VZMacOSRestoreImage *, NSError *)) {
      [VZMacOSRestoreImage loadFileURL:url completionHandler:handler];
    });
  }
  CAMLreturn(result);
}

CAMLprim value vz_restore_image_url(value v_image)
{
  CAMLparam1(v_image);
  CAMLlocal1(result);
  @autoreleasepool {
    VZMacOSRestoreImage *image = vz_unwrap(v_image);
    result = caml_copy_string(image.URL.absoluteString.UTF8String);
  }
  CAMLreturn(result);
}

CAMLprim value vz_restore_image_requirements(value v_image)
{
  CAMLparam1(v_image);
  CAMLlocal1(result);
  @autoreleasepool {
    VZMacOSRestoreImage *image = vz_unwrap(v_image);
    VZMacOSConfigurationRequirements *requirements =
      image.mostFeaturefulSupportedConfiguration;
    result = (requirements == nil) ? Val_int(0) : vz_block1(0, vz_alloc(requirements));
  }
  CAMLreturn(result);
}

CAMLprim value vz_requirements_hardware_model(value v_requirements)
{
  CAMLparam1(v_requirements);
  CAMLlocal1(result);
  @autoreleasepool {
    VZMacOSConfigurationRequirements *requirements = vz_unwrap(v_requirements);
    result = vz_alloc(requirements.hardwareModel);
  }
  CAMLreturn(result);
}

CAMLprim value vz_requirements_minimum_cpu_count(value v_requirements)
{
  CAMLparam1(v_requirements);
  VZMacOSConfigurationRequirements *requirements = vz_unwrap(v_requirements);
  CAMLreturn(Val_long(requirements.minimumSupportedCPUCount));
}

CAMLprim value vz_requirements_minimum_memory_size(value v_requirements)
{
  CAMLparam1(v_requirements);
  CAMLlocal1(result);
  VZMacOSConfigurationRequirements *requirements = vz_unwrap(v_requirements);
  result = caml_copy_int64(requirements.minimumSupportedMemorySize);
  CAMLreturn(result);
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

CAMLprim value vz_configuration_create_macos(value v_cpu, value v_mem, value v_platform)
{
  CAMLparam3(v_cpu, v_mem, v_platform);
  CAMLlocal1(result);
  @autoreleasepool {
    VZVirtualMachineConfiguration *config = [[VZVirtualMachineConfiguration alloc] init];
    config.CPUCount = Long_val(v_cpu);
    config.memorySize = Int64_val(v_mem);
    config.platform = vz_unwrap(v_platform);
    config.bootLoader = [[VZMacOSBootLoader alloc] init];
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
  CAMLlocal1(result);
  @autoreleasepool {
    VZVirtualMachineConfiguration *config = vz_unwrap(v_config);
    NSError *error = nil;
    result = [config validateWithError:&error]
               ? Val_int(0)
               : vz_block1(0, caml_copy_string(error.localizedDescription.UTF8String));
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
