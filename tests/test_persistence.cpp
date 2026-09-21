#include "test_framework.h"
#include "core/config.h"
#include "data/database.h"
#include "game/chart_loader.h"
#include "game/replay.h"
#include "utils/file_io.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cmath>

namespace {
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("sakura-io-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){std::filesystem::create_directories(path);}
    ~Temp(){sakura::data::Database::GetInstance().Shutdown();std::error_code ec;std::filesystem::remove_all(path,ec);}
};
}
TEST_CASE("Settings recover malformed JSON and clamp invalid values", "[config]") {
    Temp temp;auto& cfg=sakura::core::Config::GetInstance();const auto snapshot=cfg.GetRoot();const auto previous=cfg.GetFilePath();
    const auto file=temp.path/"settings.json";
    REQUIRE(sakura::utils::AtomicWrite(file,"{broken"));REQUIRE(!cfg.Load(file.string()));REQUIRE(cfg.Get<float>("gameplay.note_speed",0)>0);
    REQUIRE(sakura::utils::AtomicWrite(file,R"({"audio":{"master_volume":9},"gameplay":{"note_speed":-5},"input":{"key_lane_0":4,"key_lane_1":4}})"));
    REQUIRE(cfg.Load(file.string()));REQUIRE(cfg.Get<float>("audio.master_volume")==1);
    REQUIRE(cfg.Get<float>("gameplay.note_speed")>=0.5f);
    REQUIRE(cfg.Get<int>("input.key_lane_0")!=cfg.Get<int>("input.key_lane_1"));
    cfg.Set("audio.master_volume",0.37f);REQUIRE(cfg.Save());REQUIRE(cfg.Load(file.string()));
    REQUIRE(std::abs(cfg.Get<float>("audio.master_volume")-0.37f)<0.001f);
    if(!previous.empty())cfg.Load(previous);cfg.Restore(snapshot);
}
TEST_CASE("A failed atomic replacement preserves the original file", "[io]") {
    Temp temp;const auto file=temp.path/"record.json";
    REQUIRE(sakura::utils::AtomicWrite(file,"original"));std::filesystem::create_directory(file.string()+".tmp");
    REQUIRE(!sakura::utils::AtomicWrite(file,"replacement"));std::ifstream in(file);std::string value;in>>value;REQUIRE(value=="original");
}
TEST_CASE("Malformed nested chart data and replay events fail gracefully", "[validation]") {
    Temp temp;sakura::game::ChartLoader loader;
    auto file=temp.path/"bad.json";
    REQUIRE(sakura::utils::AtomicWrite(file,R"({"id":"empty","title":"Empty","difficulties":[]})"));REQUIRE(!loader.LoadChartInfo(file.string()));
    REQUIRE(sakura::utils::AtomicWrite(file,R"({"timing_points":[{"time":0,"bpm":120}],"keyboard_notes":[{"time":100,"lane":99}]})"));REQUIRE(!loader.LoadChartData(file.string()));
    REQUIRE(sakura::utils::AtomicWrite(file,R"({"timing_points":[{"time":0,"bpm":120}],"mouse_notes":[{"time":100,"type":"slider","slider_duration":1000,"slider_path":[null]}]})"));REQUIRE(!loader.LoadChartData(file.string()));
    REQUIRE(sakura::utils::AtomicWrite(file,R"({"format":"sakura-replay","version":1,"chart_id":"x","chart_hash":"0","difficulty":0,"rate":1,"start_ms":0,"inputs":[[20,0,0,0,0],[10,0,0,0,0]]})"));REQUIRE(!sakura::game::Replay::Load(file.string()));
}
TEST_CASE("Assisted play is excluded and database backups restore a consistent snapshot", "[database]") {
    Temp temp;auto& db=sakura::data::Database::GetInstance();db.Shutdown();REQUIRE(db.Initialize((temp.path/"live.db").string()));
    sakura::game::GameResult result;result.chartId="song";result.difficulty="Normal";result.score=800000;result.assisted=true;
    REQUIRE(!db.SaveScore(result));REQUIRE(db.GetTotalPlayCount()==0);
    result.assisted=false;REQUIRE(db.SaveScore(result));REQUIRE(db.BackupTo((temp.path/"backup.db").string()));
    result.score=900000;REQUIRE(db.SaveScore(result));REQUIRE(db.GetTotalPlayCount()==2);
    REQUIRE(db.RestoreFrom((temp.path/"backup.db").string()));REQUIRE(db.GetTotalPlayCount()==1);REQUIRE(db.GetBestScore("song","Normal")->score==800000);
    REQUIRE(sakura::utils::AtomicWrite(temp.path/"invalid.db","not a database"));REQUIRE(!db.RestoreFrom((temp.path/"invalid.db").string()));
    REQUIRE(db.GetBestScore("song","Normal")->score==800000);
}
TEST_CASE("Legacy score migration preserves the old table without mixing scoring systems", "[database][migration]") {
    Temp temp;auto file=(temp.path/"legacy.db").string();sqlite3* raw=nullptr;REQUIRE(sqlite3_open(file.c_str(),&raw)==SQLITE_OK);
    REQUIRE(sqlite3_exec(raw,"CREATE TABLE scores(score INTEGER);INSERT INTO scores VALUES(1095000)",nullptr,nullptr,nullptr)==SQLITE_OK);sqlite3_close(raw);
    auto& db=sakura::data::Database::GetInstance();db.Shutdown();REQUIRE(db.Initialize(file));REQUIRE(db.GetAllBestScores().empty());db.Shutdown();
    REQUIRE(sqlite3_open(file.c_str(),&raw)==SQLITE_OK);sqlite3_stmt* statement=nullptr;
    REQUIRE(sqlite3_prepare_v2(raw,"SELECT score FROM legacy_scores_v1",-1,&statement,nullptr)==SQLITE_OK);REQUIRE(sqlite3_step(statement)==SQLITE_ROW);REQUIRE(sqlite3_column_int(statement,0)==1095000);
    sqlite3_finalize(statement);sqlite3_close(raw);
}
TEST_CASE("Edited chart revisions keep separate best scores", "[database][revision]") {
    Temp temp;auto& db=sakura::data::Database::GetInstance();db.Shutdown();REQUIRE(db.Initialize((temp.path/"live.db").string()));
    sakura::game::GameResult r;r.chartId="same";r.difficulty="Normal";r.chartHash="old";r.score=900000;REQUIRE(db.SaveScore(r));
    r.chartHash="new";r.score=800000;REQUIRE(db.SaveScore(r));
    REQUIRE(db.GetBestScore("same","Normal","new")->score==800000);
    REQUIRE(!db.GetBestScore("same","Normal","unknown"));
    REQUIRE(db.GetBestScore("same","Normal","old")->score==900000);
}
TEST_CASE("Future backup versions and failed migrations preserve the active database", "[database][restore]") {
    Temp temp;auto& db=sakura::data::Database::GetInstance();db.Shutdown();REQUIRE(db.Initialize((temp.path/"live.db").string()));
    sakura::game::GameResult r;r.chartId="preserve";r.score=1234;REQUIRE(db.SaveScore(r));
    auto backup=(temp.path/"backup.db").string();REQUIRE(db.BackupTo(backup));
    sqlite3* raw=nullptr;REQUIRE(sqlite3_open(backup.c_str(),&raw)==SQLITE_OK);
    REQUIRE(sqlite3_exec(raw,"PRAGMA user_version=999",nullptr,nullptr,nullptr)==SQLITE_OK);sqlite3_close(raw);
    REQUIRE(!db.RestoreFrom(backup));REQUIRE(db.GetTotalPlayCount()==1);REQUIRE(db.GetHighestScore()==1234);
    REQUIRE(sqlite3_open(backup.c_str(),&raw)==SQLITE_OK);
    REQUIRE(sqlite3_exec(raw,"PRAGMA user_version=0;CREATE TABLE legacy_scores_v1(id INTEGER)",nullptr,nullptr,nullptr)==SQLITE_OK);sqlite3_close(raw);
    REQUIRE(!db.RestoreFrom(backup));REQUIRE(db.GetTotalPlayCount()==1);REQUIRE(db.GetHighestScore()==1234);
}
