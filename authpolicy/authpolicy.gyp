{
  'target_defaults': {
    'cflags': [
      '-Wshadow',
      '-Wextra',
    ],
  },
  'targets': [
    # D-Bus code generator.
    {
      'target_name': 'dbus_code_generator',
      'type': 'none',
      'variables': {
        'dbus_service_config': 'dbus_bindings/dbus-service-config.json',
        'dbus_adaptors_out_dir': 'include/authpolicy',
      },
      'sources': [
        'dbus_bindings/org.chromium.AuthPolicy.xml',
      ],
      'includes': ['../common-mk/generate-dbus-adaptors.gypi'],
    },
    # Container protos
    {
      'target_name': 'container-protos',
      'type': 'static_library',
      'variables': {
        'proto_in_dir': 'proto',
        'proto_out_dir': 'include/bindings',
      },
      'sources': [
        '<(proto_in_dir)/authpolicy_containers.proto',
      ],
      'includes': ['../common-mk/protoc.gypi'],
    },
    # Autogenerated policy sources
    {
      'target_name': 'policy_code_generator',
      'type': 'none',
      'hard_dependency': 1,
      'variables': {
        'policy_tools_dir': '<(sysroot)/usr/share/policy_tools',
        'policy_resources_dir': '<(sysroot)/usr/share/policy_resources',
        'out_dir': '<(SHARED_INTERMEDIATE_DIR)/include/bindings',
      },
      'actions': [{
        'action_name': 'run_generate_script',
        'inputs': [
          '<(policy_tools_dir)/generate_policy_source.py',
          '<(policy_resources_dir)/policy_templates.json',
          '<(policy_resources_dir)/VERSION',
        ],
        'outputs': [
          '<(out_dir)/policy_constants.h',
          '<(out_dir)/policy_constants.cc',
        ],
        'action': [
          'python', '<(policy_tools_dir)/generate_policy_source.py',
          '--cros-policy-constants-header=<(out_dir)/policy_constants.h',
          '--cros-policy-constants-source=<(out_dir)/policy_constants.cc',
          '<(policy_resources_dir)/VERSION',
          '<(OS)',
          '1',         # chromeos-flag
          '<(policy_resources_dir)/policy_templates.json',
        ],
      }],
    },
    # Authpolicy library.
    {
      'target_name': 'libauthpolicy',
      'type': 'static_library',
      'dependencies': [
        '../common-mk/external_dependencies.gyp:policy-protos',
        '../common-mk/external_dependencies.gyp:user_policy-protos',
        'container-protos',
        'dbus_code_generator',
        'policy_code_generator',
      ],
      'variables': {
        'gen_src_in_dir': '<(SHARED_INTERMEDIATE_DIR)/include/bindings',
        'deps': [
          'dbus-1',
          'libbrillo-<(libbase_ver)',
          'libchrome-<(libbase_ver)',
          'libpcrecpp',
        ],
      },
      'sources': [
        '<(gen_src_in_dir)/policy_constants.cc',
        'anonymizer.cc',
        'authpolicy.cc',
        'authpolicy_flags.cc',
        'authpolicy_metrics.cc',
        'constants.cc',
        'jail_helper.cc',
        'path_service.cc',
        'platform_helper.cc',
        'policy/device_policy_encoder.cc',
        'policy/extension_policy_encoder.cc',
        'policy/policy_encoder_helper.cc',
        'policy/preg_policy_encoder.cc',
        'policy/user_policy_encoder.cc',
        'process_executor.cc',
        'samba_helper.cc',
        'samba_interface.cc',
        'tgt_manager.cc',
      ],
    },
    # Parser tool.
    {
      'target_name': 'authpolicy_parser',
      'type': 'executable',
      'dependencies': ['libauthpolicy'],
      'variables': {
        'deps': [
          'libbrillo-<(libbase_ver)',
          'libcap',
          'libchrome-<(libbase_ver)',
          'libmetrics-<(libbase_ver)',
          'libminijail',
          'libpcrecpp',
          # system_api depends on protobuf (or protobuf-lite). It must
          # appear before protobuf or the linker flags won't be in the right
          # order.
          'system_api',
          'protobuf-lite',
        ],
      },
      'sources': [
        'authpolicy_parser_main.cc',
      ],
    },
    # Authpolicy daemon executable.
    {
      'target_name': 'authpolicyd',
      'type': 'executable',
      'dependencies': [
        'libauthpolicy',
        'authpolicy_parser',
      ],
      'variables': {
        'deps': [
          'libbrillo-<(libbase_ver)',
          'libcap',
          'libchrome-<(libbase_ver)',
          'libmetrics-<(libbase_ver)',
          'libminijail',
          'libpcrecpp',
          # system_api depends on protobuf (or protobuf-lite). It must appear
          # before protobuf or the linker flags won't be in the right order.
          'system_api',
          'protobuf-lite',
        ],
      },
      'sources': ['authpolicy_main.cc'],
      'link_settings': {
        'libraries': [
          '-linstallattributes-<(libbase_ver)',
        ],
      },
    },
  ],
  # Unit tests.
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'authpolicy_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'defines': ['UNIT_TEST'],
          'dependencies': [
            'libauthpolicy',
            'stub_common',
          ],
          'variables': {
            'deps': [
              'libbrillo-<(libbase_ver)',
              'libcap',
              'libchrome-<(libbase_ver)',
              'libchrome-test-<(libbase_ver)',
              'libmetrics-<(libbase_ver)',
              'libminijail',
              'libpcrecpp',
              # system_api depends on protobuf (or protobuf-lite). It must
              # appear before protobuf or the linker flags won't be in the right
              # order.
              'system_api',
              'protobuf-lite',
            ],
          },
          'sources': [
            'anonymizer_unittest.cc',
            'authpolicy_flags_unittest.cc',
            'authpolicy_testrunner.cc',
            'authpolicy_unittest.cc',
            'policy/device_policy_encoder_unittest.cc',
            'policy/extension_policy_encoder_unittest.cc',
            'policy/preg_policy_encoder_unittest.cc',
            'policy/preg_policy_writer.cc',
            'policy/user_policy_encoder_unittest.cc',
            'process_executor_unittest.cc',
            'samba_helper_unittest.cc',
          ],
        },
        {
          'target_name': 'stub_common',
          'type': 'static_library',
          'variables': {
            'deps': [
              'libchrome-<(libbase_ver)',
            ],
          },
          'sources': ['stub_common.cc'],
        },
        {
          'target_name': 'stub_net',
          'type': 'executable',
          'dependencies': [
            'libauthpolicy',
            'stub_common',
          ],
          'variables': {
            'deps': [
              'libcap',
              'libchrome-<(libbase_ver)',
              'libpcrecpp',
            ],
          },
          'sources': ['stub_net_main.cc'],
        },
        {
          'target_name': 'stub_kinit',
          'type': 'executable',
          'dependencies': [
            'libauthpolicy',
            'stub_common',
          ],
          'variables': {
            'deps': [
              'libcap',
              'libchrome-<(libbase_ver)',
              'libpcrecpp',
            ],
          },
          'sources': ['stub_kinit_main.cc'],
        },
        {
          'target_name': 'stub_klist',
          'type': 'executable',
          'dependencies': [
            'libauthpolicy',
            'stub_common',
          ],
          'variables': {
            'deps': [
              'libchrome-<(libbase_ver)',
              'libpcrecpp',
            ],
          },
          'sources': ['stub_klist_main.cc'],
        },
        {
          'target_name': 'stub_smbclient',
          'type': 'executable',
          'dependencies': [
            'libauthpolicy',
            'stub_common',
          ],
          'variables': {
            'deps': [
              'libchrome-<(libbase_ver)',
              'libpcrecpp',
            ],
          },
          'sources': ['stub_smbclient_main.cc'],
        },
      ],
    }],
  ],
}
