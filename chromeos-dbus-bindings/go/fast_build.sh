#!/bin/bash -e

# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script quickly builds the go-generate-chromeos-dbus-bindings executable
# or its unit tests within a Chrome OS chroot.

# Personal Go workspace used to cache compiled packages.
readonly GOHOME="${HOME}/go"

# Directory where compiled packages are cached.
readonly PKGDIR="${GOHOME}/pkg"

# Go workspaces containing the source.
readonly SRCDIRS=(
  "${HOME}/chromiumos/src/platform2/chromeos-dbus-bindings/go"
)

# Package to build to produce the generator executable.
readonly GENERATOR_PKG="go.chromium.org/chromiumos/dbusbindings/cmd/generator"

# Output filename for generator executable.
readonly GENERATOR_OUT="${GOHOME}/bin/go-generate-chromeos-dbus-bindings"

# Readonly Go workspaces containing source to build. Note that the packages
# installed to /usr/lib/gopath (dev-go/crypto, dev-go/subcommand, etc.) need to
# be emerged beforehand.
GOPATH="$(IFS=:; echo "${SRCDIRS[*]}"):/usr/lib/gopath"
export GOPATH
export GO111MODULE=off # enforce GOPATH mode

# Disable cgo and PIE on building generator binaries. See:
# https://crbug.com/976196
# https://github.com/golang/go/issues/30986#issuecomment-475626018
export CGO_ENABLED=0
export GOPIE=0

CMD=$(basename "${0}")
readonly CMD

# Prints usage information and exits.
usage() {
  cat - <<EOF >&2
Quickly builds the generator executable or its unit tests.

Usage:
 ${CMD}                                               Builds generator to
 ${CMD//?/ }                                               ${GENERATOR_OUT}.
 ${CMD} -d                                            Builds generator with
 ${CMD//?/ }                                               debugging symbols to
 ${CMD//?/ }                                               ${GENERATOR_OUT}.
 ${CMD} -b <pkg> -o <path>                            Builds <pkg> to <path>.
 ${CMD} [-v] -T [-- <gotest opts>]                    Tests all packages.
 ${CMD} [-v] [-r <regex>] -t <pkg> [-- <gotest opts>] Tests <pkg>.
 ${CMD} -C                                            Checks all code using
 ${CMD//?/ }                                               "go vet".
 ${CMD} -c <pkg>                                      Checks <pkg>'s code.

EOF
  exit 1
}

# Prints all packages containing a file with the given suffix.
get_pkgs_with_file_suffix() {
  local suffix="$1"

  local dir
  for dir in "${SRCDIRS[@]}"; do
    if [[ -d "${dir}/src" ]]; then
      (cd "${dir}/src"
      find -L . -name "*${suffix}" -exec dirname {} + | sort | uniq | cut -b 3-)
    fi
  done
}

# Prints all checkable packages.
get_check_pkgs() {
  get_pkgs_with_file_suffix ".go"
}

# Prints all testable packages.
get_test_pkgs() {
  get_pkgs_with_file_suffix "_test.go"
}

# Builds an executable package to a destination path.
run_build() {
  local pkg="${1}"
  local dest="${2}"
  if [[ "${debug}" == 0 ]]; then
    go build -i -pkgdir "${PKGDIR}" -o "${dest}" "${pkg}"
  else
    go build -gcflags="all=-N -l" -i -pkgdir "${PKGDIR}" -o "${dest}" "${pkg}"
  fi
}

# Checks one or more packages.
run_vet() {
  go vet -unusedresult.funcs=errors.New,errors.Wrap,errors.Wrapf,fmt.Errorf,\
fmt.Sprint,fmt.Sprintf,sort.Reverse "${@}"
}

# Tests one or more packages.
run_test() {
  local args=("${@}" "${EXTRAARGS[@]}")
  # Do not double quote ${verbose_flag} to avoid it being treated as empty path
  # to a package
  # shellcheck disable=SC2086
  go test ${verbose_flag} -pkgdir "${PKGDIR}" \
    ${test_regex:+"-run=${test_regex}"} "${args[@]}"
}

# Executable package to build.
build_pkg=

# Path to which executable package should be installed.
build_out=

# Package to check via "go vet".
check_pkg=

# Test package to build and run.
test_pkg=

# Verbose flag for testing.
verbose_flag=

# Test regex list for unit testing.
test_regex=

# Whether to build and run a debug package.
debug=0

while getopts "CTdb:c:ho:r:t:v-" opt; do
  case "${opt}" in
    C)
      check_pkg=all
      ;;
    T)
      test_pkg=all
      ;;
    d)
      debug=1
      ;;
    b)
      build_pkg="${OPTARG}"
      ;;
    c)
      check_pkg="${OPTARG}"
      ;;
    o)
      build_out="${OPTARG}"
      ;;
    r)
      test_regex="${OPTARG}"
      ;;
    t)
      test_pkg="${OPTARG}"
      ;;
    v)
      verbose_flag="-v"
      ;;
    *)
      usage
      ;;
  esac
done

shift $((OPTIND-1))
EXTRAARGS=( "$@" )

if [ -n "${build_pkg}" ]; then
  if [ -z "${build_out}" ]; then
    echo "Required output file missing: -o <path>" >&2
    exit 1
  fi
  run_build "${build_pkg}" "${build_out}"
elif [ -n "${test_pkg}" ]; then
  if [ "${test_pkg}" = 'all' ]; then
    run_test "$(get_test_pkgs)"
  else
    run_test "${test_pkg}"
  fi
elif [ -n "${check_pkg}" ]; then
  if [ "${check_pkg}" = 'all' ]; then
    run_vet "$(get_check_pkgs)"
  else
    run_vet "${check_pkg}"
  fi
else
  run_build "${GENERATOR_PKG}" "${GENERATOR_OUT}"
fi
