// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package configs

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"strconv"
	"strings"
)

// OptionType enumerates the list of supported config option types.
type OptionType string

const (
	// Switch is the type for a switch-based option that is meant to be
	// turned on or off.
	Switch OptionType = "switch"

	// Number is the type for a number-based option with a value descriptor
	// of type NumberValueDescriptor.
	Number = "number"

	// Map is the type for a map-based option that can hold a wide variety
	// of key-value pairs.
	Map = "map"

	// Selection is the type for a selection-based option with a value
	// descriptor of type SelectionValueDescriptor.
	Selection = "selection"

	// Unknow means the type is unrecognized.
	Unknown = "unknown"
)

// NumberValueDescriptor describes the value metadata of a number option.
type NumberValueDescriptor struct {
	Min  float64 `json:"min,omitempty"`
	Max  float64 `json:"max,omitempty"`
	Step float64 `json:"step,omitempty"`
}

// Enum describes an enum value for a selection option.
type Enum struct {
	Desc      string  `json:"desc,omitempty"`
	EnumValue float64 `json:"enum_value,omitempty"`
}

// SelectionValueDescriptor describes the value metadata of a selection option.
type SelectionValueDescriptor struct {
	Enums []Enum
}

// OptionDescriptor describes the general metadata of a config option.
type OptionDescriptor struct {
	Name            string      `json:"name,omitempty"`
	Key             string      `json:"key,omitempty"`
	Summary         string      `json:"summary,omitempty"`
	Type            OptionType  `json:"type,omitempty"`
	ValueDescriptor interface{} `json:"value_descriptor,omitempty"`
	Default         interface{} `json:"default,omitempty"`
	Value           interface{} `json:"value,omitempty"`
}

// ConfigDescriptor describes the metadata of a config setting.
type ConfigDescriptor struct {
	Name               string              `json:"name,omitempty"`
	Key                string              `json:"key,omitempty"`
	Summary            string              `json:"summary,omitempty"`
	DefaultConfigFile  string              `json:"default_config_file,omitempty"`
	OverrideConfigFile string              `json:"override_config_file,omitempty"`
	Options            []*OptionDescriptor `json:"options,omitempty"`
}

// validateOptionType validates the loaded JSON data in |opt.Type| and
// converts the data to the corresponding OptionType.
func (opt *OptionDescriptor) validateOptionType() error {
	switch strings.ToLower(string(opt.Type)) {
	case "switch":
		opt.Type = Switch
	case "number":
		opt.Type = Number
	case "map":
		opt.Type = Map
	case "selection":
		opt.Type = Selection
	default:
		return fmt.Errorf("invalid option type: %s", opt.Type)
	}
	return nil
}

// validateValueDescriptor validates the loaded JSON data in
// |opt.ValueDescriptor| and converts the data to the corresponding value
// descriptor interface.
func (opt *OptionDescriptor) validateValueDescriptor() error {
	switch opt.Type {
	case Switch:
		if opt.ValueDescriptor != nil {
			return fmt.Errorf("switch option does not need value descriptor")
		}

	case Number:
		var m = opt.ValueDescriptor.(map[string]interface{})
		for _, k := range []string{"min", "max", "step"} {
			if m[k] == nil {
				return fmt.Errorf("number option must specify %q in value descriptor", k)
			}
		}
		tmp, err := json.Marshal(m)
		if err != nil {
			return fmt.Errorf("failed to convert %s to NumberValueDescriptor: %w", m, err)
		}
		var vd NumberValueDescriptor
		err = json.Unmarshal(tmp, &vd)
		if err != nil {
			return fmt.Errorf("failed to convert %s to NumberValueDescriptor: %w", m, err)
		}
		opt.ValueDescriptor = vd

	case Map:
		if opt.ValueDescriptor != nil {
			return fmt.Errorf("map option does not need value descriptor")
		}

	case Selection:
		var m = opt.ValueDescriptor.([]interface{})
		// TODO: Check duplicate values
		tmp, err := json.Marshal(m)
		if err != nil {
			return fmt.Errorf("failed to convert %s to SelectionValueDescriptor: %w", m, err)
		}
		var vd SelectionValueDescriptor
		err = json.Unmarshal(tmp, &vd.Enums)
		if err != nil {
			return fmt.Errorf("failed to convert %s to SelectionValueDescriptor: %w", m, err)
		}
		opt.ValueDescriptor = vd

	default:
		return fmt.Errorf("invalid option type: %s", opt.Type)
	}
	return nil
}

// validateValue validates the given |val| based on the value requirement of
// |opt.Type|.
func (opt *OptionDescriptor) validateValue(val interface{}) error {
	switch opt.Type {
	case Switch:
		switch val.(type) {
		case bool:
			return nil
		default:
			return fmt.Errorf("switch option %q must have boolean value", opt.Key)
		}

	case Number:
		switch val.(type) {
		case float64:
			return nil
		default:
			return fmt.Errorf("number option %q must have float value", opt.Key)
		}

	case Map:
		switch val.(type) {
		case map[string]interface{}:
			return nil
		default:
			return fmt.Errorf("map option %q must have dict value", opt.Key)
		}

	case Selection:
		switch val.(type) {
		case float64:
			return nil
		default:
			return fmt.Errorf("selection option %q must have float value", opt.Key)
		}

	default:
		return fmt.Errorf("invalid option type: %s", opt.Type)
	}
}

// Validate validates the loaded JSON content in |opt|.
func (opt *OptionDescriptor) Validate() error {
	if len(opt.Name) == 0 {
		return fmt.Errorf("name cannot be empty")
	}
	if len(opt.Key) == 0 {
		return fmt.Errorf("key cannot be empty")
	}
	if err := opt.validateOptionType(); err != nil {
		return err
	}
	if err := opt.validateValueDescriptor(); err != nil {
		return err
	}
	if err := opt.validateValue(opt.Default); err != nil {
		return err
	}

	// Initialize |Value| to the default value.
	if opt.Value == nil {
		opt.Value = opt.Default
	}
	if err := opt.validateValue(opt.Value); err != nil {
		return err
	}

	return nil
}

// Update updates validates |newValue| and updates |opt.Value|.
func (opt *OptionDescriptor) Update(newValue string) error {
	switch opt.Type {
	case Switch:
		v, err := strconv.ParseBool(newValue)
		if err != nil {
			return err
		}
		opt.Value = v
	case Number:
		v, err := strconv.ParseFloat(newValue, 64)
		if err != nil {
			return err
		}
		vd := opt.ValueDescriptor.(NumberValueDescriptor)
		if v < vd.Min || v > vd.Max {
			return fmt.Errorf("%s must be in the range [%v, %v]", opt.Name, vd.Min, vd.Max)
		}
		opt.Value = v
	case Map:
		var v map[string]interface{}
		if err := json.Unmarshal([]byte(newValue), &v); err != nil {
			return err
		}
		opt.Value = v
	case Selection:
		v, err := strconv.ParseFloat(newValue, 64)
		if err != nil {
			return err
		}
		opt.Value = float64(v)
	}
	return nil
}

// Validate validates the loaded JSON content in |conf|.
func (conf *ConfigDescriptor) Validate() error {
	if len(conf.Name) == 0 {
		return fmt.Errorf("name must be set in the descriptor of %q", conf.Name)
	}
	if len(conf.DefaultConfigFile) == 0 {
		return fmt.Errorf("default_config_file must be set in the descriptor of %q", conf.Name)
	}
	if len(conf.OverrideConfigFile) == 0 {
		return fmt.Errorf("override_config_file must be set in the descriptor of %q", conf.Name)
	}
	return nil
}

// LoadDescriptor loads the config file descriptor from |file|.
func (conf *ConfigDescriptor) LoadDescriptor(file string) error {
	data, err := ioutil.ReadFile(file)
	if err != nil {
		return err
	}
	if err := json.Unmarshal(data, conf); err != nil {
		return err
	}
	if err := conf.Validate(); err != nil {
		return err
	}
	for _, opt := range conf.Options {
		if err := opt.Validate(); err != nil {
			return err
		}
	}
	return nil
}

// LoadDeviceSettings loads the config options values store in the config file
// paths specified in |conf| and sets the values in |conf.Options|.
func (conf *ConfigDescriptor) LoadDeviceSettings() error {
	var loadSettings = func(file string) error {
		data, err := ioutil.ReadFile(file)
		if err != nil {
			return fmt.Errorf("failed to load config file %q: %w", file, err)
		}
		settings := make(map[string]interface{})
		if err := json.Unmarshal(data, &settings); err != nil {
			return fmt.Errorf("failed to parse config file %q: %w", file, err)
		}

		// Sets the loaded value if there's a matching option with the
		// same key.
		for k, v := range settings {
			for _, opt := range conf.Options {
				if opt.Key == k {
					opt.Value = v
					if err := opt.validateValue(opt.Value); err != nil {
						return err
					}
				}
			}
		}
		log.Printf("Loaded device settings from file: %q", file)
		return nil
	}

	// Default config file is required.
	if err := loadSettings(conf.DefaultConfigFile); err != nil {
		return err
	}

	// Override config file is optional.
	loadSettings(conf.OverrideConfigFile)

	return nil
}

// SaveDeviceSettings saves the config option values stored in |conf.Options|
// into the config file paths specified in |conf|. SaveDeviceSettings will
// only overwrite config option values in the settings files with matching
// option key, or add new option values into the file. SaveDeviceSettings will
// not remove option values in the config file that it does not recognize.
func (conf *ConfigDescriptor) SaveDeviceSettings() error {
	var loadSettings = func(file string) map[string]interface{} {
		if _, err := os.Stat(file); errors.Is(err, os.ErrNotExist) {
			return nil
		}
		// Load the existing settings in the file.
		data, err := ioutil.ReadFile(file)
		if err != nil {
			log.Printf("failed to read existing settings from file %q", file)
			return nil
		}
		settings := make(map[string]interface{})
		if err := json.Unmarshal(data, &settings); err != nil {
			log.Printf("failed to unmarshal existing settings from file %q", file)
			return nil
		}
		return settings
	}

	var saveSettings = func(file string) error {
		f, err := os.OpenFile(file, os.O_RDWR|os.O_CREATE, 0644)
		if err != nil {
			return fmt.Errorf("failed to open settings file %q: %w", file, err)
		}
		defer f.Close()

		// Load the existing settings in the file.
		settings := loadSettings(file)
		if settings == nil {
			// The file may not exist.
			settings = make(map[string]interface{})
		}

		// Update the existing settings with our descriptor.
		for _, opt := range conf.Options {
			settings[opt.Key] = opt.Value
		}
		data, err := json.MarshalIndent(settings, "", "  ")
		if err != nil {
			return fmt.Errorf("failed to marshal settings %s: %w", settings, err)
		}
		length, err := f.Write(data)
		if err != nil {
			return fmt.Errorf("failed to write settings to %q: %w", file, err)
		}
		f.Truncate(int64(length))
		log.Printf("Save device settings to file: %q", file)
		return nil
	}

	if err := saveSettings(conf.OverrideConfigFile); err != nil {
		return err
	}
	return nil
}
