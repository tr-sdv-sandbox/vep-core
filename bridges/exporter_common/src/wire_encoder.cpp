// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "wire_encoder.hpp"

namespace vep::exporter {

vep::transfer::Quality convert_quality(vep_VssQuality dds_quality) {
    switch (dds_quality) {
        case vep_VSS_QUALITY_VALID:
            return vep::transfer::QUALITY_VALID;
        case vep_VSS_QUALITY_INVALID:
            return vep::transfer::QUALITY_INVALID;
        case vep_VSS_QUALITY_NOT_AVAILABLE:
        default:
            return vep::transfer::QUALITY_NOT_AVAILABLE;
    }
}

void convert_struct_field(const vep_VssStructField& dds_field,
                          vep::transfer::StructField* pb_field) {
    if (dds_field.name) {
        pb_field->set_name(dds_field.name);
    }

    switch (dds_field.type) {
        case vep_VSS_VALUE_TYPE_BOOL:
            pb_field->set_bool_val(dds_field.bool_value);
            break;
        case vep_VSS_VALUE_TYPE_INT8:
            pb_field->set_int32_val(static_cast<int32_t>(dds_field.int8_value));
            break;
        case vep_VSS_VALUE_TYPE_INT16:
            pb_field->set_int32_val(dds_field.int16_value);
            break;
        case vep_VSS_VALUE_TYPE_INT32:
            pb_field->set_int32_val(dds_field.int32_value);
            break;
        case vep_VSS_VALUE_TYPE_INT64:
            pb_field->set_int64_val(dds_field.int64_value);
            break;
        case vep_VSS_VALUE_TYPE_UINT8:
            pb_field->set_uint32_val(dds_field.uint8_value);
            break;
        case vep_VSS_VALUE_TYPE_UINT16:
            pb_field->set_uint32_val(dds_field.uint16_value);
            break;
        case vep_VSS_VALUE_TYPE_UINT32:
            pb_field->set_uint32_val(dds_field.uint32_value);
            break;
        case vep_VSS_VALUE_TYPE_UINT64:
            pb_field->set_uint64_val(dds_field.uint64_value);
            break;
        case vep_VSS_VALUE_TYPE_FLOAT:
            pb_field->set_float_val(dds_field.float_value);
            break;
        case vep_VSS_VALUE_TYPE_DOUBLE:
            pb_field->set_double_val(dds_field.double_value);
            break;
        case vep_VSS_VALUE_TYPE_STRING:
            pb_field->set_string_val(dds_field.string_value ? dds_field.string_value : "");
            break;

        // Array types
        case vep_VSS_VALUE_TYPE_BOOL_ARRAY: {
            auto* arr = pb_field->mutable_bool_array();
            if (dds_field.bool_array._buffer) {
                for (uint32_t i = 0; i < dds_field.bool_array._length; ++i) {
                    arr->add_values(dds_field.bool_array._buffer[i]);
                }
            }
            break;
        }
        case vep_VSS_VALUE_TYPE_INT32_ARRAY: {
            auto* arr = pb_field->mutable_int32_array();
            if (dds_field.int32_array._buffer) {
                for (uint32_t i = 0; i < dds_field.int32_array._length; ++i) {
                    arr->add_values(dds_field.int32_array._buffer[i]);
                }
            }
            break;
        }
        case vep_VSS_VALUE_TYPE_INT64_ARRAY: {
            auto* arr = pb_field->mutable_int64_array();
            if (dds_field.int64_array._buffer) {
                for (uint32_t i = 0; i < dds_field.int64_array._length; ++i) {
                    arr->add_values(dds_field.int64_array._buffer[i]);
                }
            }
            break;
        }
        case vep_VSS_VALUE_TYPE_FLOAT_ARRAY: {
            auto* arr = pb_field->mutable_float_array();
            if (dds_field.float_array._buffer) {
                for (uint32_t i = 0; i < dds_field.float_array._length; ++i) {
                    arr->add_values(dds_field.float_array._buffer[i]);
                }
            }
            break;
        }
        case vep_VSS_VALUE_TYPE_DOUBLE_ARRAY: {
            auto* arr = pb_field->mutable_double_array();
            if (dds_field.double_array._buffer) {
                for (uint32_t i = 0; i < dds_field.double_array._length; ++i) {
                    arr->add_values(dds_field.double_array._buffer[i]);
                }
            }
            break;
        }
        case vep_VSS_VALUE_TYPE_STRING_ARRAY: {
            auto* arr = pb_field->mutable_string_array();
            if (dds_field.string_array._buffer) {
                for (uint32_t i = 0; i < dds_field.string_array._length; ++i) {
                    arr->add_values(dds_field.string_array._buffer[i] ? dds_field.string_array._buffer[i] : "");
                }
            }
            break;
        }

        default:
            break;
    }
}

void convert_struct_value(const vep_VssStructValue& dds_struct,
                          vep::transfer::StructValue* pb_struct) {
    if (dds_struct.type_name) {
        pb_struct->set_type_name(dds_struct.type_name);
    }

    if (dds_struct.fields._buffer) {
        for (uint32_t i = 0; i < dds_struct.fields._length; ++i) {
            auto* pb_field = pb_struct->add_fields();
            convert_struct_field(dds_struct.fields._buffer[i], pb_field);
        }
    }
}

bool convert_value_to_signal(const vep_VssValue& dds_value,
                             vep::transfer::Signal* pb_signal) {
    switch (dds_value.type) {
        case vep_VSS_VALUE_TYPE_BOOL:
            pb_signal->set_bool_val(dds_value.bool_value);
            return true;
        case vep_VSS_VALUE_TYPE_INT8:
            pb_signal->set_int32_val(static_cast<int32_t>(dds_value.int8_value));
            return true;
        case vep_VSS_VALUE_TYPE_INT16:
            pb_signal->set_int32_val(dds_value.int16_value);
            return true;
        case vep_VSS_VALUE_TYPE_INT32:
            pb_signal->set_int32_val(dds_value.int32_value);
            return true;
        case vep_VSS_VALUE_TYPE_INT64:
            pb_signal->set_int64_val(dds_value.int64_value);
            return true;
        case vep_VSS_VALUE_TYPE_UINT8:
            pb_signal->set_uint32_val(dds_value.uint8_value);
            return true;
        case vep_VSS_VALUE_TYPE_UINT16:
            pb_signal->set_uint32_val(dds_value.uint16_value);
            return true;
        case vep_VSS_VALUE_TYPE_UINT32:
            pb_signal->set_uint32_val(dds_value.uint32_value);
            return true;
        case vep_VSS_VALUE_TYPE_UINT64:
            pb_signal->set_uint64_val(dds_value.uint64_value);
            return true;
        case vep_VSS_VALUE_TYPE_FLOAT:
            pb_signal->set_float_val(dds_value.float_value);
            return true;
        case vep_VSS_VALUE_TYPE_DOUBLE:
            pb_signal->set_double_val(dds_value.double_value);
            return true;
        case vep_VSS_VALUE_TYPE_STRING:
            pb_signal->set_string_val(dds_value.string_value ? dds_value.string_value : "");
            return true;

        // Array types
        case vep_VSS_VALUE_TYPE_BOOL_ARRAY: {
            auto* arr = pb_signal->mutable_bool_array();
            if (dds_value.bool_array._buffer) {
                for (uint32_t i = 0; i < dds_value.bool_array._length; ++i) {
                    arr->add_values(dds_value.bool_array._buffer[i]);
                }
            }
            return true;
        }
        case vep_VSS_VALUE_TYPE_INT8_ARRAY: {
            auto* arr = pb_signal->mutable_int32_array();
            if (dds_value.int8_array._buffer) {
                for (uint32_t i = 0; i < dds_value.int8_array._length; ++i) {
                    arr->add_values(static_cast<int32_t>(dds_value.int8_array._buffer[i]));
                }
            }
            return true;
        }
        case vep_VSS_VALUE_TYPE_INT16_ARRAY: {
            auto* arr = pb_signal->mutable_int32_array();
            if (dds_value.int16_array._buffer) {
                for (uint32_t i = 0; i < dds_value.int16_array._length; ++i) {
                    arr->add_values(dds_value.int16_array._buffer[i]);
                }
            }
            return true;
        }
        case vep_VSS_VALUE_TYPE_INT32_ARRAY: {
            auto* arr = pb_signal->mutable_int32_array();
            if (dds_value.int32_array._buffer) {
                for (uint32_t i = 0; i < dds_value.int32_array._length; ++i) {
                    arr->add_values(dds_value.int32_array._buffer[i]);
                }
            }
            return true;
        }
        case vep_VSS_VALUE_TYPE_INT64_ARRAY: {
            auto* arr = pb_signal->mutable_int64_array();
            if (dds_value.int64_array._buffer) {
                for (uint32_t i = 0; i < dds_value.int64_array._length; ++i) {
                    arr->add_values(dds_value.int64_array._buffer[i]);
                }
            }
            return true;
        }
        case vep_VSS_VALUE_TYPE_UINT8_ARRAY: {
            auto* arr = pb_signal->mutable_uint32_array();
            if (dds_value.uint8_array._buffer) {
                for (uint32_t i = 0; i < dds_value.uint8_array._length; ++i) {
                    arr->add_values(dds_value.uint8_array._buffer[i]);
                }
            }
            return true;
        }
        case vep_VSS_VALUE_TYPE_UINT16_ARRAY: {
            auto* arr = pb_signal->mutable_uint32_array();
            if (dds_value.uint16_array._buffer) {
                for (uint32_t i = 0; i < dds_value.uint16_array._length; ++i) {
                    arr->add_values(dds_value.uint16_array._buffer[i]);
                }
            }
            return true;
        }
        case vep_VSS_VALUE_TYPE_UINT32_ARRAY: {
            auto* arr = pb_signal->mutable_uint32_array();
            if (dds_value.uint32_array._buffer) {
                for (uint32_t i = 0; i < dds_value.uint32_array._length; ++i) {
                    arr->add_values(dds_value.uint32_array._buffer[i]);
                }
            }
            return true;
        }
        case vep_VSS_VALUE_TYPE_UINT64_ARRAY: {
            auto* arr = pb_signal->mutable_uint64_array();
            if (dds_value.uint64_array._buffer) {
                for (uint32_t i = 0; i < dds_value.uint64_array._length; ++i) {
                    arr->add_values(dds_value.uint64_array._buffer[i]);
                }
            }
            return true;
        }
        case vep_VSS_VALUE_TYPE_FLOAT_ARRAY: {
            auto* arr = pb_signal->mutable_float_array();
            if (dds_value.float_array._buffer) {
                for (uint32_t i = 0; i < dds_value.float_array._length; ++i) {
                    arr->add_values(dds_value.float_array._buffer[i]);
                }
            }
            return true;
        }
        case vep_VSS_VALUE_TYPE_DOUBLE_ARRAY: {
            auto* arr = pb_signal->mutable_double_array();
            if (dds_value.double_array._buffer) {
                for (uint32_t i = 0; i < dds_value.double_array._length; ++i) {
                    arr->add_values(dds_value.double_array._buffer[i]);
                }
            }
            return true;
        }
        case vep_VSS_VALUE_TYPE_STRING_ARRAY: {
            auto* arr = pb_signal->mutable_string_array();
            if (dds_value.string_array._buffer) {
                for (uint32_t i = 0; i < dds_value.string_array._length; ++i) {
                    arr->add_values(dds_value.string_array._buffer[i] ? dds_value.string_array._buffer[i] : "");
                }
            }
            return true;
        }

        // Struct types
        case vep_VSS_VALUE_TYPE_STRUCT: {
            auto* pb_struct = pb_signal->mutable_struct_val();
            convert_struct_value(dds_value.struct_value, pb_struct);
            return true;
        }
        case vep_VSS_VALUE_TYPE_STRUCT_ARRAY: {
            auto* pb_arr = pb_signal->mutable_struct_array();
            if (dds_value.struct_array._buffer) {
                for (uint32_t i = 0; i < dds_value.struct_array._length; ++i) {
                    auto* pb_struct = pb_arr->add_values();
                    convert_struct_value(dds_value.struct_array._buffer[i], pb_struct);
                }
            }
            return true;
        }

        case vep_VSS_VALUE_TYPE_EMPTY:
            return true;

        default:
            return false;
    }
}

void encode_vss_signal(const vep_VssSignal& msg,
                       vep::transfer::Signal* pb_signal,
                       int64_t base_timestamp_ms) {
    pb_signal->set_path(msg.path ? msg.path : "");
    int64_t ts_ms = msg.header.timestamp_ns / 1000000;
    pb_signal->set_timestamp_delta_ms(static_cast<uint32_t>(ts_ms - base_timestamp_ms));
    pb_signal->set_quality(convert_quality(msg.quality));
    convert_value_to_signal(msg.value, pb_signal);
}

void encode_event(const vep_Event& msg,
                  vep::transfer::Event* pb_event,
                  int64_t base_timestamp_ms) {
    pb_event->set_event_id(msg.event_id ? msg.event_id : "");
    int64_t ts_ms = msg.header.timestamp_ns / 1000000;
    pb_event->set_timestamp_delta_ms(static_cast<uint32_t>(ts_ms - base_timestamp_ms));
    pb_event->set_category(msg.category ? msg.category : "");
    pb_event->set_event_type(msg.event_type ? msg.event_type : "");
    pb_event->set_severity(static_cast<vep::transfer::Severity>(msg.severity));
}

void encode_gauge(const vep_OtelGauge& msg,
                  vep::transfer::Metric* pb_metric,
                  int64_t base_timestamp_ms) {
    pb_metric->set_name(msg.name ? msg.name : "");
    int64_t ts_ms = msg.header.timestamp_ns / 1000000;
    pb_metric->set_timestamp_delta_ms(static_cast<uint32_t>(ts_ms - base_timestamp_ms));
    pb_metric->set_gauge(msg.value);

    // Add source_id as a label
    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        pb_metric->add_label_keys("service");
        pb_metric->add_label_values(msg.header.source_id);
    }

    for (uint32_t i = 0; i < msg.labels._length; ++i) {
        if (msg.labels._buffer[i].key) {
            pb_metric->add_label_keys(msg.labels._buffer[i].key);
            pb_metric->add_label_values(
                msg.labels._buffer[i].value ? msg.labels._buffer[i].value : "");
        }
    }
}

void encode_counter(const vep_OtelCounter& msg,
                    vep::transfer::Metric* pb_metric,
                    int64_t base_timestamp_ms) {
    pb_metric->set_name(msg.name ? msg.name : "");
    int64_t ts_ms = msg.header.timestamp_ns / 1000000;
    pb_metric->set_timestamp_delta_ms(static_cast<uint32_t>(ts_ms - base_timestamp_ms));
    pb_metric->set_counter(msg.value);

    // Add source_id as a label
    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        pb_metric->add_label_keys("service");
        pb_metric->add_label_values(msg.header.source_id);
    }

    for (uint32_t i = 0; i < msg.labels._length; ++i) {
        if (msg.labels._buffer[i].key) {
            pb_metric->add_label_keys(msg.labels._buffer[i].key);
            pb_metric->add_label_values(
                msg.labels._buffer[i].value ? msg.labels._buffer[i].value : "");
        }
    }
}

void encode_histogram(const vep_OtelHistogram& msg,
                      vep::transfer::Metric* pb_metric,
                      int64_t base_timestamp_ms) {
    pb_metric->set_name(msg.name ? msg.name : "");
    int64_t ts_ms = msg.header.timestamp_ns / 1000000;
    pb_metric->set_timestamp_delta_ms(static_cast<uint32_t>(ts_ms - base_timestamp_ms));

    auto* hist = pb_metric->mutable_histogram();
    hist->set_sample_count(msg.sample_count);
    hist->set_sample_sum(msg.sample_sum);

    for (uint32_t i = 0; i < msg.buckets._length; ++i) {
        hist->add_bucket_bounds(msg.buckets._buffer[i].upper_bound);
        hist->add_bucket_counts(msg.buckets._buffer[i].cumulative_count);
    }

    // Add source_id as a label
    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        pb_metric->add_label_keys("service");
        pb_metric->add_label_values(msg.header.source_id);
    }

    for (uint32_t i = 0; i < msg.labels._length; ++i) {
        if (msg.labels._buffer[i].key) {
            pb_metric->add_label_keys(msg.labels._buffer[i].key);
            pb_metric->add_label_values(
                msg.labels._buffer[i].value ? msg.labels._buffer[i].value : "");
        }
    }
}

void encode_log(const vep_OtelLogEntry& msg,
                vep::transfer::LogEntry* pb_log,
                int64_t base_timestamp_ms) {
    int64_t ts_ms = msg.header.timestamp_ns / 1000000;
    pb_log->set_timestamp_delta_ms(static_cast<uint32_t>(ts_ms - base_timestamp_ms));
    pb_log->set_level(static_cast<vep::transfer::LogLevel>(msg.level));
    pb_log->set_component(msg.component ? msg.component : "");
    pb_log->set_message(msg.message ? msg.message : "");

    // Add source_id as an attribute
    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        pb_log->add_attr_keys("service");
        pb_log->add_attr_values(msg.header.source_id);
    }

    for (uint32_t i = 0; i < msg.attributes._length; ++i) {
        if (msg.attributes._buffer[i].key) {
            pb_log->add_attr_keys(msg.attributes._buffer[i].key);
            pb_log->add_attr_values(
                msg.attributes._buffer[i].value ? msg.attributes._buffer[i].value : "");
        }
    }
}

}  // namespace vep::exporter
