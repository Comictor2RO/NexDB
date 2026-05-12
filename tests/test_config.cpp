#include <gtest/gtest.h>
#include "../Config/Config.hpp"
#include <fstream>
#include <cstdio>

static const char* TEST_CONFIG = "test_config_tmp.json";

static void writeConfig(const std::string &content)
{
    std::ofstream f(TEST_CONFIG);
    f << content;
}

class ConfigTest : public ::testing::Test {
protected:
    void TearDown() override {
        std::remove(TEST_CONFIG);
    }
};

TEST_F(ConfigTest, DefaultsWhenFileIsMissing)
{
    Config cfg = Config::load("nonexistent_config.json");
    EXPECT_EQ(cfg.port,             3000);
    EXPECT_EQ(cfg.page_size,        4096);
    EXPECT_EQ(cfg.cache_capacity,   128);
    EXPECT_EQ(cfg.aux_max_failures, 3);
    EXPECT_EQ(cfg.aux_timeout,      30);
}

TEST_F(ConfigTest, LoadsAllFields)
{
    writeConfig(R"({
  "port": 8080,
  "page_size": 4096,
  "cache_capacity": 64,
  "aux_max_failures": 5,
  "aux_timeout": 60
})");

    Config cfg = Config::load(TEST_CONFIG);
    EXPECT_EQ(cfg.port,             8080);
    EXPECT_EQ(cfg.page_size,        4096);
    EXPECT_EQ(cfg.cache_capacity,   64);
    EXPECT_EQ(cfg.aux_max_failures, 5);
    EXPECT_EQ(cfg.aux_timeout,      60);
}

TEST_F(ConfigTest, PartialFileKeepsDefaults)
{
    writeConfig(R"({
  "port": 9000
})");

    Config cfg = Config::load(TEST_CONFIG);
    EXPECT_EQ(cfg.port,             9000);
    EXPECT_EQ(cfg.cache_capacity,   128);
    EXPECT_EQ(cfg.aux_max_failures, 3);
    EXPECT_EQ(cfg.aux_timeout,      30);
}

TEST_F(ConfigTest, EmptyFileKeepsDefaults)
{
    writeConfig("{}");

    Config cfg = Config::load(TEST_CONFIG);
    EXPECT_EQ(cfg.port,           3000);
    EXPECT_EQ(cfg.cache_capacity, 128);
}
