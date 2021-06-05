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

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_regex.h>
#include <fluent-bit/flb_slist.h>
#include <fluent-bit/multiline/flb_ml.h>

struct to_state {
    struct flb_ml_rule *rule;
    struct mk_list _head;
};

struct flb_slist_entry *get_start_state(struct mk_list *list)
{
    struct mk_list *head;
    struct flb_slist_entry *e;

    mk_list_foreach(head, list) {
        e = mk_list_entry(head, struct flb_slist_entry, _head);
        if (strcmp(e->str, "start_state") == 0) {
            return e;
        }
    }

    return NULL;
}

int flb_ml_rule_create(struct flb_ml *ml,
                              flb_sds_t from_states,
                              char *regex_pattern,
                              flb_sds_t to_state,
                              char *end_pattern)
{
    int ret;
    struct flb_ml_rule *rule;

    rule = flb_calloc(1, sizeof(struct flb_ml_rule));
    if (!rule) {
        flb_errno();
        return -1;
    }
    flb_slist_create(&rule->from_states);
    mk_list_init(&rule->to_state_map);
    mk_list_add(&rule->_head, &ml->regex_rules);

    /* from_states */
    ret = flb_slist_split_string(&rule->from_states, from_states, ',', -1);
    if (ret <= 0) {
        flb_error("[multiline] rule is empty or has invalid 'from_states' tokens");
        flb_ml_rule_destroy(rule);
        return -1;
    }

    /* Check if the rule contains a 'start_state' */
    if (get_start_state(&rule->from_states)) {
        rule->start_state = FLB_TRUE;
    }

    /* regex content pattern */
    rule->regex = flb_regex_create(regex_pattern);
    if (!rule->regex) {
        flb_ml_rule_destroy(rule);
        return -1;
    }

    /* to_state */
    rule->to_state = flb_sds_create(to_state);
    if (!rule->to_state) {
        flb_ml_rule_destroy(rule);
        return -1;
    }

    /* regex end pattern */
    if (end_pattern) {
        rule->regex_end = flb_regex_create(end_pattern);
        if (!rule->regex_end) {
            flb_ml_rule_destroy(rule);
            return -1;
        }
    }

    return 0;
}

void flb_ml_rule_destroy(struct flb_ml_rule *rule)
{
    struct mk_list *tmp;
    struct mk_list *head;
    struct to_state *st;

    flb_slist_destroy(&rule->from_states);

    if (rule->regex) {
        flb_regex_destroy(rule->regex);
    }


    if (rule->to_state) {
        flb_sds_destroy(rule->to_state);
    }

    mk_list_foreach_safe(head, tmp, &rule->to_state_map) {
        st = mk_list_entry(head, struct to_state, _head);
        mk_list_del(&st->_head);
        flb_free(st);
    }

    if (rule->regex_end) {
        flb_regex_destroy(rule->regex_end);
    }

    mk_list_del(&rule->_head);
    flb_free(rule);
}

void flb_ml_rule_destroy_all(struct flb_ml *ml)
{
    struct mk_list *tmp;
    struct mk_list *head;
    struct flb_ml_rule *rule;

    mk_list_foreach_safe(head, tmp, &ml->regex_rules) {
        rule = mk_list_entry(head, struct flb_ml_rule, _head);
        flb_ml_rule_destroy(rule);
    }
}

static inline int to_states_matches_rule(struct flb_ml_rule *rule,
                                         flb_sds_t state)
{
    struct mk_list *head;
    struct flb_slist_entry *e;

    mk_list_foreach(head, &rule->from_states) {
        e = mk_list_entry(head, struct flb_slist_entry, _head);
        if (strcmp(e->str, state) == 0) {
            return FLB_TRUE;
        }
    }

    return FLB_FALSE;
}

static int set_to_state_map(struct flb_ml *ml,
                            struct flb_ml_rule *rule)
{
    int ret;
    struct to_state *s;
    struct mk_list *head;
    struct flb_ml_rule *r;

    if (!rule->to_state) {
        /* no to_state */
        return 0;
    }

    /* Iterate all rules that matches the to_state */
    mk_list_foreach(head, &ml->regex_rules) {
        r = mk_list_entry(head, struct flb_ml_rule, _head);

        /*
         * A rule can have many 'from_states', check if the current 'rule->to_state'
         * matches any 'r->from_states'
         */
        ret = to_states_matches_rule(r, rule->to_state);
        if (!ret) {
            continue;
        }

        /* We have a match. Create a 'to_state' entry into the 'to_state_map' list */
        s = flb_malloc(sizeof(struct to_state));
        if (!s) {
            flb_errno();
            return -1;
        }
        s->rule = r;
        mk_list_add(&s->_head, &rule->to_state_map);
    }

    return 0;
}

static int try_flushing_buffer(struct flb_ml *ml,
                               struct flb_ml_stream *mst,
                               struct flb_ml_stream_group *group)
{
    int next_start = FLB_FALSE;
    struct mk_list *head;
    struct to_state *st;
    struct flb_ml_rule *rule;

    rule = group->rule_to_state;
    if (!rule) {
        if (flb_sds_len(group->buf) > 0) {
            flb_ml_flush_stream_group(ml, mst, group);
            group->first_line = FLB_TRUE;
        }
        return 0;
    }

    /* Check if any 'to_state_map' referenced rules is a possible start */
    mk_list_foreach(head, &rule->to_state_map) {
        st = mk_list_entry(head, struct to_state, _head);
        if (st->rule->start_state) {
            next_start = FLB_TRUE;
            break;
        }
    }

    if (next_start && flb_sds_len(group->buf) > 0) {
        flb_ml_flush_stream_group(ml, mst, group);
        group->first_line = FLB_TRUE;
    }

    return 0;
}

/* Initialize all rules */
int flb_ml_rule_init(struct flb_ml *ml)
{
    int ret;
    struct mk_list *head;
    struct flb_ml_rule *rule;

    /* FIXME: sort rules by start_state first (let's trust in the caller) */

    /* For each rule, compose it to_state_map list */
    mk_list_foreach(head, &ml->regex_rules) {
        rule = mk_list_entry(head, struct flb_ml_rule, _head);
        /* Populate 'rule->to_state_map' list */
        ret = set_to_state_map(ml, rule);
        if (ret == -1) {
            return -1;
        }
    }

    return 0;
}


int flb_ml_rule_process(struct flb_ml *ml,
                        struct flb_ml_stream *mst,
                        struct flb_ml_stream_group *group,
                        msgpack_object *full_map,
                        void *buf, size_t size, struct flb_time *tm,
                        msgpack_object *val_content,
                        msgpack_object *val_pattern)
{
    int ret;
    char *buf_data = NULL;
    size_t buf_size = 0;
    struct mk_list *head;
    struct to_state *st = NULL;
    struct flb_ml_rule *rule = NULL;
    struct flb_ml_rule *tmp_rule = NULL;

    if (val_content) {
        buf_data = (char *) val_content->via.str.ptr;
        buf_size = val_content->via.str.size;
    }
    else {
        buf_data = buf;
        buf_size = size;
    }

    if (group->first_line) {
        group->rule_to_state = NULL;

        /* If a previous content exists, just flush it */
        if (flb_sds_len(group->buf) > 0) {
            flb_ml_flush_stream_group(ml, mst, group);
        }

        /* If this is a first line, check for any rule that matches a start state */
        mk_list_foreach(head, &ml->regex_rules) {
            rule = mk_list_entry(head, struct flb_ml_rule, _head);

            /* Is this rule matching a start_state ? */
            if (!rule->start_state) {
                rule = NULL;
                continue;
            }

            /* Matched a start_state. Check if we have a regex match */
            ret = flb_regex_match(rule->regex,
                                  (unsigned char *) buf_data,
                                  buf_size);
            if (ret) {
                /* Regex matched */
                flb_sds_cat_safe(&group->buf, buf_data, buf_size);
                group->first_line = FLB_FALSE;

                /* Copy full map content in stream buffer */
                flb_ml_register_context(ml, mst, group, tm, full_map);
                break;
            }
            rule = NULL;
        }
    }
    else if (group->rule_to_state) {
        tmp_rule = group->rule_to_state;
        rule = NULL;

        /* Lookup all possible next rules by state reference */
        mk_list_foreach(head, &tmp_rule->to_state_map) {
            st = mk_list_entry(head, struct to_state, _head);

            /* Try regex match */
            ret = flb_regex_match(st->rule->regex,
                                  (unsigned char *) buf_data, buf_size);
            if (ret) {
                /* Regex matched */
                flb_sds_cat_safe(&group->buf, buf_data, buf_size);
                rule = st->rule;
                break;
            }
            rule = NULL;
        }
    }

    /*
     * If 'rule' is set means we got a rule regex  match. This rule might have a
     * to_state defined.
     */
    if (rule) {
        /*
         * reference the rule that recently matched the pattern. So on the
         * next iteration we can query the possible 'to_states' in the list.
         */
        group->rule_to_state = rule;
        try_flushing_buffer(ml, mst, group);
    }
    else {
        /* Flush any previous content */
        group->rule_to_state = NULL;
        try_flushing_buffer(ml, mst, group);

        /* Append un-matched content to buffer and flush */
        flb_ml_register_context(ml, mst, group, tm, full_map);
        flb_sds_cat_safe(&group->buf, buf_data, buf_size);
        try_flushing_buffer(ml, mst, group);
    }

    return 0;
}
