#ifndef ACPI_INIT_H
#define ACPI_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

	void initialize_acpi(void);
	void acpi_tables(void);
	int acpi_command(int argc, char** argv);
#ifdef __cplusplus
}
#endif

#endif // ACPI_INIT_H