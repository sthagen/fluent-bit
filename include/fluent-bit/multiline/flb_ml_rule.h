/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2019-2021 The Fluent Bit Authors
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef FLB_ML_RULE_H
#define FLB_ML_RULE_H

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/multiline/flb_ml.h>

int flb_ml_rule_create(struct flb_ml *ml,
                       flb_sds_t from_states,
                       char *regex_pattern,
                       flb_sds_t to_state,
                       char *end_pattern);
void flb_ml_rule_destroy(struct flb_ml_rule *rule);
void flb_ml_rule_destroy_all(struct flb_ml *ml);
int flb_ml_rule_process(struct flb_ml *ml,
                        struct flb_ml_stream *mst,
                        msgpack_object *full_map,
                        void *buf, size_t size, struct flb_time *tm,
                        msgpack_object *val_content,
                        msgpack_object *val_pattern);
int flb_ml_rule_init(struct flb_ml *ml);

#endif
