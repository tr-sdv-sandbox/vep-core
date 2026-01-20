// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "vss_dbc_parser.hpp"

#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>

namespace vep {
namespace {

class VssDbcParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test JSON file
        temp_file_ = std::tmpnam(nullptr);
        temp_file_ += ".json";
    }

    void TearDown() override {
        std::remove(temp_file_.c_str());
    }

    void write_json(const std::string& content) {
        std::ofstream file(temp_file_);
        file << content;
        file.close();
    }

    std::string temp_file_;
};

TEST_F(VssDbcParserTest, LoadValidConfig) {
    write_json(R"({
        "Vehicle": {
            "type": "branch",
            "children": {
                "Cabin": {
                    "type": "branch",
                    "children": {
                        "HVAC": {
                            "type": "branch",
                            "children": {
                                "IsAirConditioningActive": {
                                    "datatype": "int8",
                                    "type": "actuator",
                                    "vss2dbc": {
                                        "message": {
                                            "name": "HVAC_Control",
                                            "canid": "0x1AFFCB02",
                                            "length_in_bytes": 8,
                                            "cycle_time_ms": 1000
                                        },
                                        "signal": {
                                            "name": "AC_Active",
                                            "bitposition": {
                                                "start": 0,
                                                "length": 8
                                            },
                                            "transform": {
                                                "offset": 10,
                                                "factor": 5
                                            },
                                            "max": 100,
                                            "min": -100
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    })");

    VssDbcParser parser;
    ASSERT_TRUE(parser.load(temp_file_));
    EXPECT_EQ(parser.mapping_count(), 1);

    auto* mapping = parser.find_mapping("Vehicle.Cabin.HVAC.IsAirConditioningActive");
    ASSERT_NE(mapping, nullptr);

    EXPECT_EQ(mapping->vss_path, "Vehicle.Cabin.HVAC.IsAirConditioningActive");
    EXPECT_EQ(mapping->datatype, "int8");
    EXPECT_EQ(mapping->message_name, "HVAC_Control");
    EXPECT_EQ(mapping->can_id, 0x1AFFCB02u);
    EXPECT_EQ(mapping->message_length, 8);
    EXPECT_EQ(mapping->cycle_time_ms, 1000u);
    EXPECT_EQ(mapping->signal_name, "AC_Active");
    EXPECT_EQ(mapping->bit_start, 0);
    EXPECT_EQ(mapping->bit_length, 8);
    EXPECT_DOUBLE_EQ(mapping->offset, 10.0);
    EXPECT_DOUBLE_EQ(mapping->factor, 5.0);
    EXPECT_DOUBLE_EQ(mapping->min_value, -100.0);
    EXPECT_DOUBLE_EQ(mapping->max_value, 100.0);
}

TEST_F(VssDbcParserTest, LoadStringEnumMapping) {
    write_json(R"({
        "Vehicle": {
            "type": "branch",
            "children": {
                "AirDistribution": {
                    "datatype": "string",
                    "type": "actuator",
                    "vss2dbc": {
                        "message": {
                            "name": "AirControl",
                            "canid": "0x100"
                        },
                        "signal": {
                            "name": "Mode",
                            "bitposition": {
                                "start": 0,
                                "length": 2
                            },
                            "transform": {
                                "mapping": {
                                    "UP": 0,
                                    "MIDDLE": 1,
                                    "DOWN": 2
                                }
                            }
                        }
                    }
                }
            }
        }
    })");

    VssDbcParser parser;
    ASSERT_TRUE(parser.load(temp_file_));

    auto* mapping = parser.find_mapping("Vehicle.AirDistribution");
    ASSERT_NE(mapping, nullptr);

    EXPECT_TRUE(mapping->has_value_mapping());
    EXPECT_EQ(mapping->value_mapping.size(), 3);
    EXPECT_EQ(mapping->value_mapping.at("UP"), 0);
    EXPECT_EQ(mapping->value_mapping.at("MIDDLE"), 1);
    EXPECT_EQ(mapping->value_mapping.at("DOWN"), 2);
}

TEST_F(VssDbcParserTest, MultipleMappings) {
    write_json(R"({
        "Vehicle": {
            "type": "branch",
            "children": {
                "Signal1": {
                    "type": "actuator",
                    "vss2dbc": {
                        "message": { "canid": "0x100" },
                        "signal": { "bitposition": { "start": 0, "length": 8 } }
                    }
                },
                "Signal2": {
                    "type": "actuator",
                    "vss2dbc": {
                        "message": { "canid": "0x200" },
                        "signal": { "bitposition": { "start": 0, "length": 16 } }
                    }
                },
                "Sensor1": {
                    "type": "sensor"
                }
            }
        }
    })");

    VssDbcParser parser;
    ASSERT_TRUE(parser.load(temp_file_));

    EXPECT_EQ(parser.mapping_count(), 2);
    EXPECT_NE(parser.find_mapping("Vehicle.Signal1"), nullptr);
    EXPECT_NE(parser.find_mapping("Vehicle.Signal2"), nullptr);
    EXPECT_EQ(parser.find_mapping("Vehicle.Sensor1"), nullptr);  // Not an actuator with vss2dbc
}

TEST_F(VssDbcParserTest, NonExistentFile) {
    VssDbcParser parser;
    EXPECT_FALSE(parser.load("/nonexistent/path.json"));
    EXPECT_EQ(parser.mapping_count(), 0);
}

TEST_F(VssDbcParserTest, InvalidJson) {
    write_json("{ invalid json }");

    VssDbcParser parser;
    EXPECT_FALSE(parser.load(temp_file_));
}

TEST_F(VssDbcParserTest, GetActuatorPaths) {
    write_json(R"({
        "Vehicle": {
            "type": "branch",
            "children": {
                "A": {
                    "type": "actuator",
                    "vss2dbc": {
                        "message": { "canid": "0x100" },
                        "signal": { "bitposition": { "start": 0, "length": 8 } }
                    }
                },
                "B": {
                    "type": "actuator",
                    "vss2dbc": {
                        "message": { "canid": "0x200" },
                        "signal": { "bitposition": { "start": 0, "length": 8 } }
                    }
                }
            }
        }
    })");

    VssDbcParser parser;
    ASSERT_TRUE(parser.load(temp_file_));

    auto paths = parser.get_actuator_paths();
    EXPECT_EQ(paths.size(), 2);
}

}  // namespace
}  // namespace vep
