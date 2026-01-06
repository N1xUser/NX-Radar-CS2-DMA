#include <ntifs.h>

extern "C" {
	NTKERNELAPI NTSTATUS MmCopyVirtualMemory(PEPROCESS SourceProcess, PVOID SourceAddress,
		PEPROCESS TargetProcess, PVOID TargetAddress,
		SIZE_T BufferSize, KPROCESSOR_MODE PreviousMode,
		PSIZE_T ReturnSize);
}

void debug_print(PCSTR text) {
#ifndef DEBUG
	UNREFERENCED_PARAMETER(text);
#endif
	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, " [*] NXWire - %s\n", text));
}

namespace driver {
	namespace codes {

		constexpr ULONG attach =
			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

		constexpr ULONG read =
			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

		constexpr ULONG write =
			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x698, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

	}

	struct Request {
		HANDLE process_id;

		PVOID target;
		PVOID buffer;

		SIZE_T size;
		SIZE_T return_size;
	};

	NTSTATUS create(PDEVICE_OBJECT device_object, PIRP irp) {
		UNREFERENCED_PARAMETER(device_object);

		irp->IoStatus.Status = STATUS_SUCCESS;
		irp->IoStatus.Information = 0;

		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return STATUS_SUCCESS;
	}

	NTSTATUS close(PDEVICE_OBJECT device_object, PIRP irp) {
		UNREFERENCED_PARAMETER(device_object);

		irp->IoStatus.Status = STATUS_SUCCESS;
		irp->IoStatus.Information = 0;

		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return STATUS_SUCCESS;
	}

	NTSTATUS device_control(PDEVICE_OBJECT device_object, PIRP irp) {
		UNREFERENCED_PARAMETER(device_object);

		debug_print("Device control callback");

		NTSTATUS status = STATUS_UNSUCCESSFUL;

		PIO_STACK_LOCATION stack_irp = IoGetCurrentIrpStackLocation(irp);

		auto request = reinterpret_cast<Request*>(irp->AssociatedIrp.SystemBuffer);

		if (stack_irp == nullptr || request == nullptr) {
			irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
			irp->IoStatus.Information = 0;
			IoCompleteRequest(irp, IO_NO_INCREMENT);
			return STATUS_INVALID_PARAMETER;
		}

		static PEPROCESS target_process = nullptr;

		const ULONG control_code = stack_irp->Parameters.DeviceIoControl.IoControlCode;

		switch (control_code) {

		case codes::attach:
			debug_print("Attach request received");
			if (target_process != nullptr) {
				ObDereferenceObject(target_process);
				target_process = nullptr;
			}
			status = PsLookupProcessByProcessId(request->process_id, &target_process);
			if (NT_SUCCESS(status)) {
				debug_print("Successfully attached to process");
			}
			else {
				debug_print("Failed to attach to process");
				target_process = nullptr;
			}
			break;

		case codes::read:
			debug_print("Read request received");
			if (target_process != nullptr) {
				status = MmCopyVirtualMemory(target_process, request->target, PsGetCurrentProcess(),
					request->buffer, request->size,
					KernelMode, &request->return_size);
				if (NT_SUCCESS(status)) {
					debug_print("Read successful");
				}
				else {
					debug_print("Read failed");
				}
			}
			else {
				debug_print("No target process attached");
				status = STATUS_INVALID_PARAMETER;
			}
			break;

		case codes::write:
			debug_print("Write request received");
			if (target_process != nullptr) {
				status = MmCopyVirtualMemory(PsGetCurrentProcess(), request->buffer, target_process,
					request->target, request->size,
					KernelMode, &request->return_size);
				if (NT_SUCCESS(status)) {
					debug_print("Write successful");
				}
				else {
					debug_print("Write failed");
				}
			}
			else {
				debug_print("No target process attached");
				status = STATUS_INVALID_PARAMETER;
			}
			break;

		default:
			debug_print("Unknown control code");
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}

		irp->IoStatus.Status = status;
		irp->IoStatus.Information = sizeof(Request);

		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return status;
	}
}

void driver_unload(PDRIVER_OBJECT driver_object) {
	debug_print("Driver unload called.");

	UNICODE_STRING sym_link = RTL_CONSTANT_STRING(L"\\DosDevices\\NXWire");
	IoDeleteSymbolicLink(&sym_link);

	if (driver_object->DeviceObject != nullptr) {
		IoDeleteDevice(driver_object->DeviceObject);
	}

	debug_print("Driver unloaded successfully.");
}

//Entry not prepared for kdmapper
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path) {
	UNREFERENCED_PARAMETER(registry_path);

	debug_print("DriverEntry called.");

	UNICODE_STRING device_name = RTL_CONSTANT_STRING(L"\\Device\\NXWire");
	UNICODE_STRING sym_link = RTL_CONSTANT_STRING(L"\\DosDevices\\NXWire");

	PDEVICE_OBJECT device_object = nullptr;
	NTSTATUS status = IoCreateDevice(driver_object, 0, &device_name, FILE_DEVICE_UNKNOWN,
		FILE_DEVICE_SECURE_OPEN, FALSE, &device_object);

	if (!NT_SUCCESS(status)) {
		debug_print("IoCreateDevice failed.");
		return status;
	}

	debug_print("Device created successfully.");

	status = IoCreateSymbolicLink(&sym_link, &device_name);

	if (!NT_SUCCESS(status)) {
		debug_print("IoCreateSymbolicLink failed.");
		IoDeleteDevice(device_object);
		return status;
	}

	debug_print("Symbolic link created successfully.");

	device_object->Flags |= DO_BUFFERED_IO;

	driver_object->MajorFunction[IRP_MJ_CREATE] = driver::create;
	driver_object->MajorFunction[IRP_MJ_CLOSE] = driver::close;
	driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = driver::device_control;
	driver_object->DriverUnload = driver_unload;

	device_object->Flags &= ~DO_DEVICE_INITIALIZING;

	debug_print("Driver initialization complete.");

	return STATUS_SUCCESS;
}
