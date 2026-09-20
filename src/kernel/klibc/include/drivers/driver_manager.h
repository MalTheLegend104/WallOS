#ifndef WALLOS_DRIVER_MANAGER_H
#define WALLOS_DRIVER_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif 

	struct wallos_device;

	typedef struct {
		int  (*probe)(struct wallos_device* dev);
		void (*attach)(struct wallos_device* dev);
		void (*detach)(struct wallos_device* dev);
	} wallos_driver_ops_t;

	typedef struct wallos_driver {
		const char* name;

		// Matching
		uint64_t match_flags;
		uint64_t match_mask;

		// Optional ID matching
		uint16_t vendor_id;
		uint16_t device_id;

		// Callbacks
		wallos_driver_ops_t ops;

		// Internal linkage
		struct wallos_driver* next;
	} wallos_driver_t;

	typedef enum {
		DM_ERROR_NONE = 0,          // No Error
		DM_ERROR_ALREADY_BOUND = 1, // Device has already been bound
		DM_ERROR_INVALID_DEV,       // Device is invalid for some reason
		DM_ERROR_DEV_NOT_FOUND,     // Device was not found
		DM_ERROR_NOSUCH_DRIVER,     // Driver was not found
		DM_ERROR_PROBE_FAILED,      // Driver rejected the device
		DM_ERROR_NULL_PARAM         // Parameter passed was NULL
	} device_manager_error_t;

	typedef device_manager_error_t dm_error_t;

	/**
	 * @brief
	 *
	 * @param driver
	 * @return dm_error_t
	 */
	dm_error_t dm_register_driver(wallos_driver_t* driver);

	/**
	 * @brief
	 *
	 * @param driver
	 * @return dm_error_t
	 */
	dm_error_t dm_remove_driver(wallos_driver_t* driver);

	/**
	 * @brief
	 *
	 * @param dev
	 * @return dm_error_t
	 */
	dm_error_t dm_bind_device(struct wallos_device* dev);

	/**
	 * @brief
	 *
	 * @param dev
	 * @return dm_error_t
	 */
	dm_error_t dm_unbind_device(struct wallos_device* dev);


	/**
	 * @brief Iterates through all registered devices and attempts to bind drivers to any device that is not currently bound.
	 */
	void dm_bind_all_registered(void);

	int driver_cli(int argc, char** argv);

#ifdef __cplusplus
}
#endif
#endif // WALLOS_DRIVER_MANAGER_H