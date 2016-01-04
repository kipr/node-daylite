{
  'targets': [
    {
      'target_name': 'node_daylite',
      'sources': [
        'node_daylite.cpp',
        'nodejs_daylite_node.cpp'
      ],
      "conditions": [
        [ 'OS=="linux"', {
            'cflags': [ '-std=c++11' ],
            'library_dirs': [
              '/usr/lib',
              '/usr/local/lib'
            ],
            'link_settings': {
              'libraries': [
                '-ldaylite',
                '-lbson-1.0'
              ]
            }
        }],
        [ 'OS=="mac"', {
            'xcode_settings': {
              'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
              'OTHER_CFLAGS': [ '-g', '-mmacosx-version-min=10.7', '-std=c++11', '-stdlib=libc++', '-O3', '-D__STDC_CONSTANT_MACROS', '-D_FILE_OFFSET_BITS=64', '-D_LARGEFILE_SOURCE', '-Wall' ],
              'OTHER_CPLUSPLUSFLAGS': [ '-g', '-mmacosx-version-min=10.7', '-std=c++11', '-stdlib=libc++', '-O3', '-D__STDC_CONSTANT_MACROS', '-D_FILE_OFFSET_BITS=64', '-D_LARGEFILE_SOURCE', '-Wall' ]
            },
            'library_dirs': [
              '/usr/lib',
              '/usr/local/lib',
            ],
            'link_settings': {
              'libraries': [
                '-ldaylite',
                '-lbson-1.0'
              ]
			}
        }],
        [ 'OS=="win"', {
            "configurations": {
              'Release': {
                'msvs_settings': {
                  'VCCLCompilerTool': {
                    'ExceptionHandling': 1,
                    'AdditionalIncludeDirectories': ['$(INSTALL_PREFIX)\\include',
                                                     '$(INSTALL_PREFIX)\\include\\libbson-1.0']
                  },
                  'VCLinkerTool': {
                    'AdditionalLibraryDirectories': ['$(INSTALL_PREFIX)\\lib'],
                    'AdditionalDependencies': [
                      'daylite.lib',
                      'bson-1.0.lib'
                    ]
                  }
                }
              },
              'Debug': {
                'msvs_settings': {
                  'VCCLCompilerTool': {
                    'ExceptionHandling': 1,
                    'AdditionalIncludeDirectories': ['$(INSTALL_PREFIX)\\include',
                                                     '$(INSTALL_PREFIX)\\include\\libbson-1.0']
                  },
                  'VCLinkerTool': {
                    'AdditionalLibraryDirectories': ['$(INSTALL_PREFIX)\\lib'],
                    'AdditionalDependencies': [
                      'daylite.lib',
                      'bson-1.0.lib'
                    ]
                  }
                }
              }
            }
        }]
      ]
    }
  ]
}
