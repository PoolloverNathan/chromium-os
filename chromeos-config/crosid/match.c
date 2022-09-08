/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "crosid.h"

/*
 * This file implements matching the current device identity to an
 * identity defined in the crosid identity table, normally stored at
 * /usr/share/chromeos-config/identity.bin.
 *
 * The file format of identity.bin is as follows:
 * - The table header (a struct crosid_table_header).
 * - N table entries (each a struct crosid_table_entry).
 * - Strings referenced by the table entries.  Each is referenced by
 *   an offset from the beginning of the strings area.
 *
 * identity.bin is generated by Python code in cros_config_schema.  If
 * the table format is adjusted here, the same changes must be made in
 * cros_config_schema.  Backwards-incompatible changes must bump the
 * version number in the header to prevent the wrong identity.bin from
 * being used with crosid.
 */

#define NO_TABLE_MATCH_ERROR                                                                  \
	"No defined device identities matched this device.\n"                                 \
	"This is likely one of the following problems:\n"                                     \
	"- You are running an OS image for the wrong board on this device\n"                  \
	"- Your device is not correctly provisioned (i.e., CBI or VPD unprovisioned)\n"       \
	"- model.yaml or Boxster has not been set up for your correctly provisioned device\n" \
	"\n"                                                                                  \
	"Hint: you can always run \"crosid -v\" for a full explanation.\n"

static char *get_string(struct crosid_table_header *table, uint32_t string_idx)
{
	char *strings = (char *)&table->entries[table->entry_count];

	return strings + string_idx;
}

static bool
check_optional_string_match(struct crosid_optional_string *device_str,
			    const char *identity_str, const char *log_name)
{
	crosid_log(LOG_DBG, "  Requires %s == \"%s\" ", log_name, identity_str);

	if (!device_str->present) {
		/*
		 * For historical reasons, an empty string will match
		 * a non-present value, as both libcros_config and
		 * mosys did it this way.
		 */
		if (strlen(identity_str) > 0) {
			crosid_log(LOG_DBG, "(mismatch, device has no %s)\n",
				   log_name);
			return false;
		}
	} else if (strcasecmp(device_str->value, identity_str)) {
		crosid_log(LOG_DBG, "(\"%s\" does not match)\n",
			   device_str->value);
		return false;
	}

	crosid_log(LOG_DBG, "(matches!)\n");
	return true;
}

static bool fdt_compatible(struct crosid_optional_string *procfs_compat,
			   const char *match)
{
	if (!procfs_compat->present) {
		crosid_log(LOG_DBG,
			   "    No compatible strings in procfs (no match)\n");
		return false;
	}

	for (size_t i = 0; i < procfs_compat->len;
	     i += strlen(procfs_compat->value + i) + 1) {
		crosid_log(LOG_DBG, "    \"%s\" == \"%s\"? ", match,
			   procfs_compat->value + i);
		if (!strncmp(match, procfs_compat->value + i, strlen(match))) {
			crosid_log(LOG_DBG, "true\n");
			return true;
		} else {
			crosid_log(LOG_DBG, "false\n");
		}
	}

	return false;
}

static bool table_entry_matches(struct crosid_table_header *table,
				int entry_idx,
				struct crosid_probed_device_data *data)
{
	struct crosid_table_entry *entry = &table->entries[entry_idx];
	const char *strings = (const char *)&table->entries[table->entry_count];
	int mismatches = 0;

	if (entry->flags & MATCH_SMBIOS_NAME) {
		mismatches += !check_optional_string_match(
			&data->smbios_name, strings + entry->smbios_name_match,
			"SMBIOS name");
	}

	if (entry->flags & MATCH_FDT_COMPATIBLE) {
		crosid_log(
			LOG_DBG,
			"  Requires one FDT compatible string matching \"%s\"\n",
			strings + entry->fdt_compatible_match);
		if (!fdt_compatible(&data->fdt_compatible,
				    strings + entry->fdt_compatible_match)) {
			mismatches++;
			crosid_log(LOG_DBG, "    Does not match\n");
		} else {
			crosid_log(LOG_DBG, "    Match FDT compatible!\n");
		}
	}

	if (entry->flags & MATCH_FRID) {
		mismatches += !check_optional_string_match(
			&data->frid, strings + entry->frid_match, "FRID");
	}

	if (entry->flags & MATCH_SKU_ID) {
		crosid_log(LOG_DBG, "  Requires SKU ID == %u ",
			   entry->sku_id_match);
		if (!data->has_sku_id) {
			crosid_log(LOG_DBG,
				   "(mismatch, as this device has no SKU)\n");
			mismatches++;
		} else if (data->sku_id != entry->sku_id_match) {
			crosid_log(LOG_DBG, "(%u does not match)\n",
				   data->sku_id);
			mismatches++;
		} else {
			crosid_log(LOG_DBG, "(matches!)\n");
		}
	}

	if (entry->flags & MATCH_CUSTOMIZATION_ID) {
		mismatches += !check_optional_string_match(
			&data->customization_id,
			strings + entry->customization_id_match,
			"customization_id");
	}

	if (data->custom_label_tag.present &&
	    strlen(data->custom_label_tag.value) > 0 &&
	    !(entry->flags & MATCH_CUSTOM_LABEL_TAG)) {
		/*
		 * If the device has a non-zero length custom label
		 * tag, but this config does not have a custom label
		 * tag, it should not match.
		 */
		crosid_log(LOG_DBG,
			   "mismatch, as the config has no custom-label-tag"
			   ", but this device does\n");
		mismatches++;
	} else if (entry->flags & MATCH_CUSTOM_LABEL_TAG) {
		mismatches += !check_optional_string_match(
			&data->custom_label_tag,
			strings + entry->custom_label_tag_match,
			"custom_label_tag");
	}

	return mismatches == 0;
}

int crosid_match(struct crosid_probed_device_data *data)
{
	int rv = -1;
	char *table_raw;
	size_t table_sz;
	struct crosid_table_header *table;

	if (crosid_read_file(UNIBUILD_CONFIG_PATH, "identity.bin", &table_raw,
			     &table_sz) < 0) {
		crosid_log(LOG_ERR,
			   "Failed to read unibuild device identity table\n");
		return -1;
	}

	table = (struct crosid_table_header *)table_raw;
	if (table_sz < sizeof(*table)) {
		crosid_log(LOG_ERR, "Identity table corrupted (too small?)\n");
		goto exit;
	}

	if (table->version != CROSID_TABLE_VERSION) {
		crosid_log(LOG_ERR,
			   "Incorrect table version: expected %u, got %u\n",
			   CROSID_TABLE_VERSION, table->version);
		goto exit;
	}

	if (table_raw + table_sz <
	    (char *)&table->entries[table->entry_count]) {
		crosid_log(LOG_ERR,
			   "Table file is too small to contain all entries\n");
		goto exit;
	}

	crosid_log(LOG_DBG, "There are %u possible identities\n",
		   table->entry_count);

	for (size_t i = 0; i < table->entry_count; i++) {
		crosid_log(LOG_DBG, "\nIdentity %zu:\n", i);
		if (table_entry_matches(table, i, data)) {
			crosid_log(LOG_DBG, "Identity %zu is a match!\n", i);
			rv = i;
			data->firmware_manifest_key = strdup(get_string(
				table,
				table->entries[i].firmware_manifest_key));
			goto exit;
		} else {
			crosid_log(LOG_DBG, "Identity %zu is incompatible.\n",
				   i);
		}
	}

	if (rv < 0)
		crosid_log(LOG_ERR, NO_TABLE_MATCH_ERROR);

exit:
	free(table_raw);
	return rv;
}
