#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "fmt/format.h"

#include "dcmtk/dcmdata/cmdlnarg.h"
#include "dcmtk/dcmdata/dcuid.h"
#include "dcmtk/oflog/oflog.h"
#include "dcmtk/ofstd/ofconapp.h"
#include "dcmtk/ofstd/ofcond.h"
#include "dcmtk/ofstd/ofexit.h"

#include "DicomAnonymizer.hpp"

void checkConflict(OFConsoleApplication &app, const char *first_opt,
                   const char *second_opt) {
  const std::string str =
      fmt::format("{} not allowed with {}", first_opt, second_opt);
  app.printError(str.c_str(), EXITCODE_COMMANDLINE_SYNTAX_ERROR);
};

void checkConflict(OFConsoleApplication &app, const char *first_opt,
                   const char *second_opt, const char *third_opt) {
  const std::string str = fmt::format("{}, {} and {} not allowed together",
                                      first_opt, second_opt, third_opt);
  app.printError(str.c_str(), EXITCODE_COMMANDLINE_SYNTAX_ERROR);
};

std::vector<std::filesystem::path>
findStudyDirectories(const OFString &root_path) {
  std::vector<std::filesystem::path> dirs{};

  for (const auto &entry : std::filesystem::directory_iterator(root_path)) {
    if (entry.is_directory()) {
      dirs.push_back(entry.path());
    }
  }
  return dirs;
};

void printMethods() {
  struct AnonProfiles {
    std::string_view option{};
    std::string_view profile{};
    std::string_view description{};
  };

  constexpr std::array<AnonProfiles, 4> methods{
      AnonProfiles{"<profile always used>",
                   "Basic Application Confidentiality Profile (DCM_113100)",
                   "basic tags: PatientName, PatientID, PatientSex, "
                   "physician tags, ..."},
      AnonProfiles{"--retain-patient-charac-tags",
                   "Retain Patient Characteristics Option (DCM_113108)",
                   "optional patient tags: PatientAge, "
                   "PatientWeight, SmokingStatus, ..."},
      AnonProfiles{"--retain-device-tags",
                   "Retain Device Identity Option (DCM_113109)",
                   "device tags: DeviceLabel, StationName, ..."},
      AnonProfiles{
          "--retain-institution-tags",
          "Retain Institution Identity Option (DCM_113112)",
          "institution tags: InstitutionAddress, InstitutionName, ..."}};

  for (const auto &m : methods) {
    fmt::print("{:<30} | {:<55} | {}\n", m.option, m.profile, m.description);
  }
};

int main(int argc, char *argv[]) {
  constexpr auto FNO_CONSOLE_APPLICATION{"fnodcmanon"};
  constexpr auto APP_VERSION{"0.5.0"};
  constexpr auto RELEASE_DATE{"2024-11-19"};
  const std::string rcsid = fmt::format(
      "${}: ver. {} rel. {}\n$dcmtk: ver. {} rel. {}", FNO_CONSOLE_APPLICATION,
      APP_VERSION, RELEASE_DATE, OFFIS_DCMTK_VERSION, OFFIS_DCMTK_RELEASEDATE);

  setupLogger(fmt::format("fno.apps.{}", FNO_CONSOLE_APPLICATION));
  OFConsoleApplication app{FNO_CONSOLE_APPLICATION, "DICOM anonymization tool",
                           rcsid.c_str()};
  OFCommandLine cmd{};

  // required params
  std::string opt_inDirectory{};

  // optional pseudoname params
  std::string opt_pseudonamePrefix{};
  E_PSEUDONAME_TYPE opt_pseudonameType = P_RANDOM_STRING;
  std::string opt_pseudonameFile{};

  // optional output methods
  std::string opt_outDirectory{"./anonymized_output"};
  std::string FNO_UID_ROOT{"1.2.840.113619.2"};
  std::string opt_rootUID{FNO_UID_ROOT};
  E_FILENAMES opt_filenameType = F_HEX;
  std::set<E_ADDIT_ANONYM_METHODS> opt_anonymizationMethods{};

  constexpr int LONGCOL{20};
  constexpr int SHORTCOL{4};
  cmd.setParamColumn(LONGCOL + SHORTCOL + 4);
  cmd.addParam("in-directory", "input directory with DICOM studies");

  cmd.setOptionColumns(LONGCOL, SHORTCOL);
  cmd.addGroup("general options:", LONGCOL, SHORTCOL + 2);
  cmd.addOption("--help", "-h", "print this help text and exit",
                OFCommandLine::AF_Exclusive);
  cmd.addOption("--version", "print version information and exit",
                OFCommandLine::AF_Exclusive);

  OFLog::addOptions(cmd);

  cmd.addGroup("anonymization options:");
  cmd.addOption("--prefix", "-p", 1, "string: prefix (default ``)",
                "pseudoname prefix to use for constructing pseudonames");
  cmd.addSubGroup("pseudoname suffix options:");
  cmd.addOption("--pseudoname-random", "-pr",
                "generate random alphanumeric string (lower/upper "
                "case + digits + duplicates) and append to "
                "<anonymized-prefix> (default)");
  cmd.addOption("--pseudoname-integer", "-pi",
                "append integer (start at 0) to <anonymized-prefix>; may "
                "overwrite existing files");
  cmd.addOption(
      "--pseudoname-file", "-pf", 1, "file: path/to/.csv",
      "read .csv with existing pseudonames and append to <anonymized-prefix>");

  cmd.addSubGroup("additional anonymization profiles:");
  cmd.addOption("--retain-patient-charac-tags", "-rpt",
                "retain patient characteristics option");
  cmd.addOption("--retain-device-tags", "-rdt",
                "retain device identity option");
  cmd.addOption("--retain-institution-tags", "-rit",
                "retain institution identity option");
  cmd.addOption("--print-anon-profiles",
                "print deidentification profiles for example tags",
                OFCommandLine::AF_Exclusive);

  cmd.addSubGroup("root UID options:");
  cmd.addOption(
      "--fno-uid-root", "-fuid",
      fmt::format("use FNO UID root: {} (default)", FNO_UID_ROOT).c_str());
  cmd.addOption("--offis-uid-root", "-ouid",
                "use OFFIS UID root: " OFFIS_UID_ROOT);

  cmd.addOption("--custom-uid-root", "-cuid", 1, "uid root: string",
                "use custom UID root");

  cmd.addGroup("output options:");
  cmd.addOption("--out-directory", "-od", 1,
                "directory: string (default `./anonymized_output`",
                "write modified files to output directory");
  cmd.addOption("--filename-hex", "-f", "filenames in hex format (default)");
  cmd.addOption("--filename-modality-sop", "+f",
                "filenames in MODALITY_SOPINSTUID format");

  prepareCmdLineArgs(argc, argv, FNO_CONSOLE_APPLICATION);
  if (app.parseCommandLine(cmd, argc, argv)) {
    if (cmd.hasExclusiveOption()) {
      if (cmd.findOption("--version")) {
        app.printHeader(OFTrue);
        return 0;
      }

      if (cmd.findOption("--print-anon-profiles")) {
        printMethods();
        return 0;
      }
    }

    cmd.getParam(1, opt_inDirectory);
    // cmd.getParam(2, opt_anonymizedPrefix);

    OFLog::configureFromCommandLine(cmd, app);

    if (cmd.findOption("--prefix"))
      app.checkValue(cmd.getValue(opt_pseudonamePrefix));

    cmd.beginOptionBlock();
    if (cmd.findOption("--pseudoname-random"))
      opt_pseudonameType = P_RANDOM_STRING;
    if (cmd.findOption("--pseudoname-integer"))
      opt_pseudonameType = P_INTEGER_ORDER;
    if (cmd.findOption("--pseudoname-file")) {
      opt_pseudonameType = P_FROM_FILE;
      app.checkValue(cmd.getValue(opt_pseudonameFile));
    }
    cmd.endOptionBlock();

    if (cmd.findOption("--fno-uid-root") &&
        cmd.findOption("--offis-uid-root") &&
        cmd.findOption("--custom-uid-root")) {
      checkConflict(app, "--fno-uid-root", "--offis-uid-root",
                    "--custom-uid-root");
    } else if (cmd.findOption("--fno-uid-root") &&
               cmd.findOption("--offis-uid-root")) {
      checkConflict(app, "--fno-uid-root", "--offis-uid-root");
    } else if (cmd.findOption("--fno-uid-root") &&
               cmd.findOption("--custom-uid-root")) {
      checkConflict(app, "--fno-uid-root", "--custom-uid-root");
    } else if (cmd.findOption("--offis-uid-root") &&
               cmd.findOption("--custom-uid-root")) {
      checkConflict(app, "--offis-uid-root", "--custom-uid-root");
    }

    cmd.beginOptionBlock();
    if (cmd.findOption("--fno-uid-root"))
      opt_rootUID = FNO_UID_ROOT;
    if (cmd.findOption("--offis-uid-root"))
      opt_rootUID = OFFIS_UID_ROOT;
    if (cmd.findOption("--custom-uid-root"))
      app.checkValue(cmd.getValue(opt_rootUID));
    cmd.endOptionBlock();

    if (cmd.findOption("--out-directory")) {
      app.checkValue(cmd.getValue(opt_outDirectory));
    }

    if (cmd.findOption("--filename-hex") &&
        cmd.findOption("--filename-modality-sop")) {
      checkConflict(app, "--filename-hex", "--filename-modality-sop");
    }

    cmd.beginOptionBlock();
    if (cmd.findOption("--filename-hex"))
      opt_filenameType = F_HEX;
    if (cmd.findOption("--filename-modality-sop"))
      opt_filenameType = F_MODALITY_SOPINSTUID;
    cmd.endOptionBlock();

    if (cmd.findOption("--retain-patient-charac-tags")) {
      opt_anonymizationMethods.insert(E_ADDIT_ANONYM_METHODS::M_113108);
    }

    if (cmd.findOption("--retain-device-tags")) {
      opt_anonymizationMethods.insert(E_ADDIT_ANONYM_METHODS::M_113109);
    }

    if (cmd.findOption("--retain-institution-tags")) {
      opt_anonymizationMethods.insert(E_ADDIT_ANONYM_METHODS::M_113112);
    }

    OFLOG_DEBUG(mainLogger, rcsid.c_str() << OFendl);
  }

  if (std::filesystem::exists(opt_inDirectory)) {
    if (!std::filesystem::is_directory(opt_inDirectory)) {
      OFLOG_ERROR(mainLogger,
                  "invalid path, not directory `" << opt_inDirectory << "`");
      return EXITCODE_COMMANDLINE_SYNTAX_ERROR;
    }

    if (std::filesystem::is_empty(opt_inDirectory)) {
      OFLOG_ERROR(mainLogger,
                  "invalid path, empty directory `" << opt_inDirectory << "`");
      return EXITCODE_COMMANDLINE_SYNTAX_ERROR;
    }
  } else {
    OFLOG_ERROR(mainLogger, "invalid path, directory not found `"
                                << opt_inDirectory << "`");
    return EXITCODE_COMMANDLINE_SYNTAX_ERROR;
  }

  std::vector<std::filesystem::path> studyDirs =
      findStudyDirectories(opt_inDirectory);

  StudyAnonymizer anonymizer{opt_pseudonamePrefix, opt_pseudonameType};

  if (anonymizer.m_pseudoname_type == P_INTEGER_ORDER) {
    fmt::print("using pseudonames as integer count order\n");
    anonymizer.m_count_width =
        static_cast<unsigned short>(std::to_string(studyDirs.size()).length());
    ++anonymizer.m_count_width;
    /* increment m_count_width by 1 for always at least one leading zero in
    formatted pseudoname:

    studies found: 5 -> string length = 1
    - normal: width = 1, PSEUDONAME_1, ..., PSEUDONAME_5
    - incremented: width = 2, PSEUDONAME_01, ..., PSEUDONAME_05
    */

  } else if (anonymizer.m_pseudoname_type == P_FROM_FILE) {
    fmt::print("using PatientID-pseudoname pairs from file `{}`\n",
               opt_pseudonameFile);
    OFCondition cond = anonymizer.readPseudonamesFromFile(opt_pseudonameFile);
    if (cond.bad()) {
      OFLOG_ERROR(mainLogger, cond.text());
      return cond.code();
    }

  } else {
    fmt::print("using pseudonames from random string generation\n");
  }

  (void)std::filesystem::create_directories(opt_outDirectory);
  OFLOG_INFO(mainLogger,
             fmt::format("created output directory `{}`", opt_outDirectory));

  std::string csvFilename{"anonym_output.csv"};
  if (!opt_pseudonamePrefix.empty()) {
    csvFilename.insert(0, opt_pseudonamePrefix);
  }

  std::ofstream outputAnonymFile{opt_outDirectory + '/' + csvFilename,
                                 std::ios::out};
  outputAnonymFile << "PatientID,PatientName,Pseudoname,StudyDate,"
                      "OldStudyInstanceUID,NewStudyInstanceUID\n";

  for (const auto &study_dir : studyDirs) {

    OFCondition cond{};
    cond = anonymizer.anonymizeStudy(study_dir, opt_outDirectory,
                                     opt_anonymizationMethods, opt_rootUID);

    // something bad happened
    if (cond.bad()) {
      const std::string msg =
          fmt::format("error while anonymizing study `{}`", study_dir.string());
      OFLOG_ERROR(mainLogger, msg.c_str());
      continue;
    }

    outputAnonymFile << fmt::format(
        "{},{},{},{},{},{}\n", anonymizer.m_old_id, anonymizer.m_old_name,
        anonymizer.m_pseudoname, anonymizer.m_study_date,
        anonymizer.m_old_studyuid, anonymizer.m_new_studyuid);
  }
  outputAnonymFile.close();

  return 0;
}
