{
  'target_defaults': {
    'variables': {
      'deps': [
        'libchrome-<(libbase_ver)',
        'libchromeos-<(libbase_ver)',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'libprotobinder',
      'type': 'shared_library',
      'variables': {
        'proto_in_dir': 'idl',
        'proto_out_dir': 'include/libprotobinder',
        'gen_bidl': 1,
        'deps': [
          'protobuf-lite',
        ],
      },
      'sources': [
        '<(proto_in_dir)/binder.proto',
        'binder_daemon.cc',
        'binder_host.cc',
        'binder_manager.cc',
        # TODO(derat): If the amount of testing-specific code ever becomes
        # substantial, move it to a separate shared library.
        'binder_manager_stub.cc',
        'binder_proxy.cc',
        'binder_proxy_interface_base.cc',
        'ibinder.cc',
        'iservice_manager.cc',
        'parcel.cc',
        'proto_util.cc',
      ],
      'includes': ['../../platform2/common-mk/protoc.gypi'],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'libprotobinder_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': ['libprotobinder'],
          'sources': [
            'libprotobinder_testrunner.cc',
            'parcel_unittest.cc',
          ],
        },
      ],
    }],
  ],
}
