#/usr/bin/env bash

set -xueo pipefail

find -type f -not -path './.git/*' \( -name '*.usf' -or -name '*.ush' -or -name '*.cs' -or -name '*.uplugin' \) -exec dos2unix {} +
find -type f -not -path './.git/*' \( -name '*.usf' -or -name '*.ush' -or -name '*.cs' -or -name '*.uplugin' \) -exec sed -i -e 's|\t|\ \ \ \ |g' -e 's|\s*$||' -e '$a\' {} +
yq -iPo yaml _clang-format
git clang-format --force --binary=`which clang-format-20` `git rev-list --max-parents=0 @` -- . ':!Source/ThirdParty'
