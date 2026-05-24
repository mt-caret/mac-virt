/* Objective-C glue to Apple's Virtualization framework.
   Compiled as Objective-C with ARC; see lib/dune for the -x/-fobjc-arc flags
   and the -framework link flags. */

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <Virtualization/Virtualization.h>

#include <stdio.h>

#define CAML_NAME_SPACE
#include <caml/alloc.h>
#include <caml/custom.h>
#include <caml/memory.h>
#include <caml/mlvalues.h>
#include <caml/threads.h>

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

/* Open the existing auxiliary storage at [v_path] (for booting a bundle). */
CAMLprim value vz_auxiliary_storage_load(value v_path)
{
  CAMLparam1(v_path);
  CAMLlocal1(result);
  @autoreleasepool {
    result = vz_alloc([[VZMacAuxiliaryStorage alloc]
        initWithURL:[NSURL fileURLWithPath:vz_nsstring(v_path)]]);
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

/* Block until [start]'s (image, error) completion handler fires, then return
   [Ok image] / [Error message]. */
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
  caml_enter_blocking_section();
  dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
  caml_leave_blocking_section();
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

/* Download the restore image referenced by [v_image] to the file [v_path],
   overwriting it if present. Blocks until the download completes, printing
   progress to stderr. Returns [None] on success or [Some message]. */
CAMLprim value vz_restore_image_download(value v_image, value v_path)
{
  CAMLparam2(v_image, v_path);
  CAMLlocal1(result);
  @autoreleasepool {
    NSURL *remote = ((VZMacOSRestoreImage *)vz_unwrap(v_image)).URL;
    NSURL *destination = [NSURL fileURLWithPath:vz_nsstring(v_path)];

    __block NSError *failure = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    NSURLSessionDownloadTask *task = [[NSURLSession sharedSession]
        downloadTaskWithURL:remote
          completionHandler:^(NSURL *location, NSURLResponse *response, NSError *error) {
            (void)response;
            if (error != nil) {
              failure = error;
            } else {
              NSFileManager *files = [NSFileManager defaultManager];
              [files removeItemAtURL:destination error:nil];
              NSError *move_error = nil;
              if (![files moveItemAtURL:location toURL:destination error:&move_error]) {
                failure = move_error;
              }
            }
            dispatch_semaphore_signal(done);
          }];
    [task resume];

    caml_enter_blocking_section();
    while (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)) != 0) {
      int64_t received = task.countOfBytesReceived;
      int64_t expected = task.countOfBytesExpectedToReceive;
      fprintf(stderr,
              "\rDownloading restore image... %.2f / %.2f GB",
              received / 1e9,
              expected / 1e9);
      fflush(stderr);
    }
    caml_leave_blocking_section();
    fprintf(stderr, "\n");

    const char *desc = failure.localizedDescription.UTF8String;
    result = (failure == nil)
               ? Val_int(0)
               : vz_block1(0, caml_copy_string(desc != NULL ? desc : "download failed"));
  }
  CAMLreturn(result);
}

/* ---- Storage device ---- */

CAMLprim value vz_storage_device_disk_image(value v_path, value v_read_only)
{
  CAMLparam2(v_path, v_read_only);
  CAMLlocal1(result);
  @autoreleasepool {
    NSError *error = nil;
    VZDiskImageStorageDeviceAttachment *attachment =
      [[VZDiskImageStorageDeviceAttachment alloc] initWithURL:[NSURL fileURLWithPath:vz_nsstring(v_path)]
                                                     readOnly:Bool_val(v_read_only)
                                                        error:&error];
    if (attachment == nil) {
      result = vz_error_of_nserror(error, "failed to attach disk image");
    } else {
      result = vz_block1(
        0, vz_alloc([[VZVirtioBlockDeviceConfiguration alloc] initWithAttachment:attachment]));
    }
  }
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

CAMLprim value vz_configuration_create_macos(value v_cpu,
                                             value v_mem,
                                             value v_platform,
                                             value v_storage_devices)
{
  CAMLparam4(v_cpu, v_mem, v_platform, v_storage_devices);
  CAMLlocal1(result);
  @autoreleasepool {
    VZVirtualMachineConfiguration *config = [[VZVirtualMachineConfiguration alloc] init];
    config.CPUCount = Long_val(v_cpu);
    config.memorySize = Int64_val(v_mem);
    config.platform = vz_unwrap(v_platform);
    config.bootLoader = [[VZMacOSBootLoader alloc] init];
    NSMutableArray *devices = [NSMutableArray array];
    for (value list = v_storage_devices; Is_block(list); list = Field(list, 1)) {
      [devices addObject:vz_unwrap(Field(list, 0))];
    }
    config.storageDevices = devices;

    /* A display, keyboard, and pointing device, so the machine renders to a
       VZVirtualMachineView and accepts input. Without a graphics device the
       view stays black even though the guest is running. */
    VZMacGraphicsDeviceConfiguration *graphics =
      [[VZMacGraphicsDeviceConfiguration alloc] init];
    graphics.displays = @[
      [[VZMacGraphicsDisplayConfiguration alloc] initWithWidthInPixels:1920
                                                        heightInPixels:1200
                                                         pixelsPerInch:80]
    ];
    config.graphicsDevices = @[ graphics ];
    config.keyboards = @[ [[VZUSBKeyboardConfiguration alloc] init] ];
    config.pointingDevices = @[ [[VZUSBScreenCoordinatePointingDeviceConfiguration alloc] init] ];

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

/* ---- Installer ---- */

/* Install macOS onto [v_vm]'s storage from the restore image at [v_ipsw_path].
   The installer is created and driven on the machine's own queue; this thread
   blocks until installation finishes, printing progress to stderr. Returns
   [None] on success or [Some message]. */
CAMLprim value vz_installer_install(value v_vm, value v_ipsw_path)
{
  CAMLparam2(v_vm, v_ipsw_path);
  CAMLlocal1(result);
  @autoreleasepool {
    vz_vm *p = Data_custom_val(v_vm);
    VZVirtualMachine *vm = (__bridge VZVirtualMachine *)p->vm;
    dispatch_queue_t queue = (__bridge dispatch_queue_t)p->queue;
    NSURL *ipsw = [NSURL fileURLWithPath:vz_nsstring(v_ipsw_path)];

    __block VZMacOSInstaller *installer = nil;
    __block NSError *failure = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    dispatch_async(queue, ^{
      installer = [[VZMacOSInstaller alloc] initWithVirtualMachine:vm restoreImageURL:ipsw];
      [installer installWithCompletionHandler:^(NSError *err) {
        failure = err;
        dispatch_semaphore_signal(done);
      }];
    });

    caml_enter_blocking_section();
    while (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)) != 0) {
      double fraction = (installer != nil) ? installer.progress.fractionCompleted : 0.0;
      fprintf(stderr, "\rInstalling macOS... %5.1f%%", fraction * 100.0);
      fflush(stderr);
    }
    caml_leave_blocking_section();
    fprintf(stderr, "\rInstalling macOS... done.   \n");

    result = (failure == nil)
               ? Val_int(0)
               : vz_block1(0, caml_copy_string(failure.localizedDescription.UTF8String));
  }
  CAMLreturn(result);
}

/* ---- GUI boot ---- */

/* Reports unexpected machine stops to stderr and quits the app. */
@interface MacVirtDelegate : NSObject <VZVirtualMachineDelegate>
@end

@implementation MacVirtDelegate
- (void)guestDidStopVirtualMachine:(VZVirtualMachine *)virtualMachine
{
  (void)virtualMachine;
  fprintf(stderr, "Guest stopped the virtual machine.\n");
  [NSApp terminate:nil];
}

- (void)virtualMachine:(VZVirtualMachine *)virtualMachine didStopWithError:(NSError *)error
{
  (void)virtualMachine;
  fprintf(stderr, "Virtual machine stopped with error: %s\n", error.localizedDescription.UTF8String);
  [NSApp terminate:nil];
}
@end

/* Create a virtual machine from [v_config] on the main queue, show it in a
   window, start it, and run the AppKit event loop until the window is closed
   (which terminates the process). Must be called on the main thread. */
CAMLprim value vz_boot_gui(value v_config, value v_title)
{
  CAMLparam2(v_config, v_title);
  @autoreleasepool {
    VZVirtualMachineConfiguration *config = vz_unwrap(v_config);
    NSString *title = vz_nsstring(v_title);

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    /* No queue argument: the machine is confined to the main queue, which the
       view and the event loop also run on. */
    VZVirtualMachine *vm = [[VZVirtualMachine alloc] initWithConfiguration:config];
    MacVirtDelegate *delegate = [[MacVirtDelegate alloc] init];
    vm.delegate = delegate;

    VZVirtualMachineView *view =
      [[VZVirtualMachineView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 800)];
    view.virtualMachine = vm;
    view.capturesSystemKeys = YES;

    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 1280, 800)
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                             | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.title = title;
    window.contentView = view;
    [window center];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowWillCloseNotification
                                                      object:window
                                                       queue:nil
                                                  usingBlock:^(NSNotification *note) {
                                                    (void)note;
                                                    [NSApp terminate:nil];
                                                  }];

    [vm startWithCompletionHandler:^(NSError *error) {
      if (error != nil) {
        fprintf(stderr, "Failed to start virtual machine: %s\n", error.localizedDescription.UTF8String);
        [NSApp terminate:nil];
      } else {
        fprintf(stderr, "Virtual machine started.\n");
      }
    }];

    [NSApp run];
    /* [NSApp terminate:] exits the process, so this is not reached on a normal
       window close; keep the delegate alive until here regardless. */
    (void)delegate;
  }
  CAMLreturn(Val_unit);
}
