# Copyright 2021 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

FROM debian:11

RUN apt-get update && apt-get install -y clang-11 llvm-11-dev libclang-11-dev clang-format-11 jq git cmake python

ENV CLANG_FORMAT=clang-format-11

RUN git clone https://github.com/include-what-you-use/include-what-you-use.git /iwyu
# latest commit on the clang 11 branch
RUN cd /iwyu && git checkout 5db414ac448004fe019871c977905cb7c2cff23f

RUN mkdir /iwyu_build && cd /iwyu_build && cmake -G "Unix Makefiles" -DCMAKE_PREFIX_PATH=/usr/lib/llvm-11 /iwyu && make

ADD iwyu.sh /

# When running locally, we'll be impersonating the current user, so we need
# to make the script runnable by everyone.
RUN chmod a+rx /iwyu.sh

CMD ["echo 'Run with tools/distrib/iwyu.sh'"]
