{
  'target_defaults': {
    'defines': [
      'OS_CHROMEOS',
      'USE_ARC=<(USE_arc)',
      'USE_NSS_CERTS',
      'USE_SYSTEMD=<(USE_systemd)',
    ],
    'variables': {
      'deps': [
        'dbus-1',
        'libbrillo-<(libbase_ver)',
        'libchrome-<(libbase_ver)',
        'libchromeos-ui-<(libbase_ver)',
        'libcontainer',
        'libmetrics-<(libbase_ver)',
        'nss',
        # system_api depends on protobuf (or protobuf-lite). It must appear
        # before protobuf here or the linker flags won't be in the right
        # order.
        'system_api',
        'protobuf-lite',
      ],
    },
    'link_settings': {
      'libraries': [
        '-lvboot_host',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'session_manager_proxies',
      'type': 'none',
      'variables': {
        'xml2cpp_type': 'proxy',
        'xml2cpp_in_dir': '.',
        'xml2cpp_out_dir': 'include/session_manager/dbus_proxies',
      },
      'sources': [
        '<(xml2cpp_in_dir)/dbus_bindings/org.chromium.SessionManagerInterface.xml',
      ],
      'includes': ['../common-mk/xml2cpp.gypi'],
    },
    {
      'target_name': 'libsession_manager',
      'type': 'static_library',
      'dependencies': [
        '../common-mk/external_dependencies.gyp:policy-protos',
      ],
      'link_settings': {
        'libraries': [
          '-lbootstat',
        ],
      },
      'sources': [
        'browser_job.cc',
        'child_exit_handler.cc',
        'child_job.cc',
        'chrome_setup.cc',
        'container_config_parser.cc',
        'crossystem.cc',
        'crossystem_impl.cc',
        'dbus_error_types.cc',
        'dbus_signal_emitter.cc',
        'device_local_account_policy_service.cc',
        'device_policy_service.cc',
        'file_checker.cc',
        'generator_job.cc',
        'key_generator.cc',
        'liveness_checker_impl.cc',
        'login_metrics.cc',
        'nss_util.cc',
        'owner_key_loss_mitigator.cc',
        'policy_key.cc',
        'policy_service.cc',
        'policy_store.cc',
        'regen_mitigator.cc',
        'server_backed_state_key_generator.cc',
        'session_manager_dbus_adaptor.cc',
        'session_manager_impl.cc',
        'session_manager_service.cc',
        'system_utils_impl.cc',
        'systemd_unit_starter.cc',
        'upstart_signal_emitter.cc',
        'user_policy_service.cc',
        'user_policy_service_factory.cc',
        'vpd_process_impl.cc'
      ],
    },
    {
      'target_name': 'keygen',
      'type': 'executable',
      'sources': [
        'keygen.cc',
        'keygen_worker.cc',
        'nss_util.cc',
        'policy_key.cc',
        'system_utils_impl.cc',
      ],
    },
    {
      'target_name': 'session_manager',
      'type': 'executable',
      'libraries': [
        '-lrootdev',
        '-lcontainer',
      ],
      'dependencies': ['libsession_manager'],
      'sources': ['session_manager_main.cc'],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'session_manager_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'defines': ['UNIT_TEST'],
          'dependencies': ['libsession_manager'],
          'variables': {
            'deps': [
              'libbrillo-test-<(libbase_ver)',
              'libchrome-test-<(libbase_ver)',
            ],
          },
          'sources': [
            'browser_job_unittest.cc',
            'child_exit_handler_unittest.cc',
            'container_config_parser_unittest.cc',
            'device_local_account_policy_service_unittest.cc',
            'device_policy_service_unittest.cc',
            'fake_browser_job.cc',
            'fake_child_process.cc',
            'fake_crossystem.cc',
            'fake_generated_key_handler.cc',
            'fake_generator_job.cc',
            'keygen_worker.cc',
            'key_generator_unittest.cc',
            'liveness_checker_impl_unittest.cc',
            'login_metrics_unittest.cc',
            'mock_constructors.cc',
            'mock_nss_util.cc',
            'mock_system_utils.cc',
            'nss_util_unittest.cc',
            'policy_key_unittest.cc',
            'policy_service_unittest.cc',
            'policy_store_unittest.cc',
            'regen_mitigator_unittest.cc',
            'server_backed_state_key_generator_unittest.cc',
            'session_manager_impl_unittest.cc',
            'session_manager_process_unittest.cc',
            'session_manager_testrunner.cc',
            'system_utils_unittest.cc',
            'user_policy_service_unittest.cc',
          ],
        },
      ],
    }],
  ],
}
