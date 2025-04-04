#include <array>
#include <string>
#include <filesystem>
#include <set>

#include "fmt/format.h"
#include "fmt/os.h"

#include "dcmtk/config/osconfig.h"
#include "dcmtk/dcmdata/dctk.h"
#include "dcmtk/dcmdata/cmdlnarg.h"
#include "dcmtk/ofstd/ofconapp.h"

#include "DicomAnonymizer.hpp"

void checkConflict(OFConsoleApplication &app, const char *first_opt, const char *second_opt) {
    const std::string str = fmt::format("{} not allowed with {}", first_opt, second_opt);
    app.printError(str.c_str(), EXITCODE_COMMANDLINE_SYNTAX_ERROR);
};

void checkConflict(OFConsoleApplication &app, const char *first_opt, const char *second_opt, const char *third_opt) {
    const std::string str = fmt::format("{}, {} and {} not allowed together", first_opt, second_opt, third_opt);
    app.printError(str.c_str(), EXITCODE_COMMANDLINE_SYNTAX_ERROR);
};

int getNumberOfStudies(const char *opt_inDirectory) {
    int studyCount{0};
    for (const auto &studyDir: std::filesystem::directory_iterator(opt_inDirectory)) {
        if (!studyDir.is_directory()) {
            OFLOG_WARN(mainLogger,
                       fmt::format(R"(Path "{}" is not directory)", studyDir.path().string()).c_str());
        }

        if (studyDir.path().empty()) {
            OFLOG_WARN(mainLogger,
                       fmt::format(R"(Directory "{}" is empty\n)",
                           studyDir.path().string()).c_str()
                      );
            continue;
        }
        ++studyCount;
    }
    return studyCount;
};

void printMethods() {
    constexpr std::array<std::string_view, 3> methods = {"M_113100", "M_113108", "M_113112"};
    constexpr std::array<std::string_view, 3> names   = {
        "Basic Application Confidentiality Profile",
        "Retain Patient Characteristics Option",
        "Retain Institution Identity Option"
    };
    constexpr std::array<std::string_view, 3> option = {"always", "optional", "optional"};
    for (int i = 0; i < methods.size(); ++i) {
        fmt::print("{:<10} | {:<45} | {:<10}\n", methods[i], names[i], option[i]);
    }
}

int main(int argc, char *argv[]) {
    constexpr auto    FNO_CONSOLE_APPLICATION{"fnodcmanon"};
    constexpr auto    APP_VERSION{"0.3.0"};
    constexpr auto    RELEASE_DATE{"2024-11-19"};
    const std::string rcsid = fmt::format("${}: ver. {} rel. {}\n$dcmtk: ver. {} rel.{}",
                                          FNO_CONSOLE_APPLICATION,
                                          APP_VERSION,
                                          RELEASE_DATE,
                                          OFFIS_DCMTK_VERSION,
                                          OFFIS_DCMTK_RELEASEDATE);

    // OFLogger mainLogger = OFLog::getLogger(fmt::format("fno.apps.{}", FNO_CONSOLE_APPLICATION).c_str());

    setupLogger(fmt::format("fno.apps.{}", FNO_CONSOLE_APPLICATION));
    OFConsoleApplication app(FNO_CONSOLE_APPLICATION, "DICOM anonymization tool", rcsid.c_str());
    OFCommandLine        cmd;

    // required params
    const char *opt_inDirectory{nullptr};
    const char *opt_anonymizedPrefix{nullptr};

    // optional params
    const char *FNO_UID_ROOT{"1.2.840.113619.2"};
    const char *opt_outDirectory{"./anonymized_output"};
    const char *opt_rootUID{nullptr};
    E_FILENAMES opt_filenameType = F_HEX;
    const char *opt_patientListFilename{"anonymized_patients.txt"};

    std::set<E_ANONYM_METHODS> opt_anonymizationMethods{M_113100};

    constexpr int LONGCOL{20};
    constexpr int SHORTCOL{4};
    cmd.setParamColumn(LONGCOL + SHORTCOL + 4);
    cmd.addParam("in-directory", "input directory with DICOM studies");
    cmd.addParam("anonymized-prefix", "pseudoname prefix overwriting DICOM tags PatientID, PatientName");

    cmd.setOptionColumns(LONGCOL, SHORTCOL);
    cmd.addGroup("general options:", LONGCOL, SHORTCOL + 2);
    cmd.addOption("--help", "-h", "print this help text and exit", OFCommandLine::AF_Exclusive);
    cmd.addOption("--version",
                  "print version information and exit",
                  OFCommandLine::AF_Exclusive);

    OFLog::addOptions(cmd);

    cmd.addGroup("anonymization options:");
    cmd.addOption("--anonym-method",
                  "-m",
                  1,
                  "method: string",
                  "add anonymization method to list (default [DCM_113100])\n"
                  "use --print-methods or -pm to print out acceptable methods and their explanation");
    cmd.addOption("--print-methods", "-pm",
                  "print deidentification methods and their explanation",
                  OFCommandLine::AF_Exclusive);
    cmd.addOption("--fno-uid-root", "-fuid", fmt::format("use FNO UID root: {} (default)", FNO_UID_ROOT).c_str());
    cmd.addOption("--offis-uid-root", "-ouid", "use OFFIS UID root: " OFFIS_UID_ROOT);
    cmd.addOption("--custom-uid-root", "-cuid", 1, "uid root: string", "use custom UID root");

    cmd.addGroup("output options:");
    cmd.addOption("--out-directory",
                  "-od",
                  1,
                  R"(directory: string (default "./anonymized_output")",
                  "write modified files to output directory");
    cmd.addOption("--filename-hex", "-f", "use integers in hex format as filenames (default)");
    cmd.addOption("--filename-modality-sop", "+f", "use Modality and SOPInstanceUIDs as filenames");
    cmd.addOption("--key-list",
                  "-k",
                  1,
                  "filename: string",
                  "set name of pre-post anonymization text file\n(default <out-directory>/anonymized_patients.txt)");

    prepareCmdLineArgs(argc, argv, FNO_CONSOLE_APPLICATION);
    if (app.parseCommandLine(cmd, argc, argv)) {
        if (cmd.hasExclusiveOption()) {
            if (cmd.findOption("--version")) {
                app.printHeader(OFTrue);
                return 0;
            }

            if (cmd.findOption("--print-methods")) {
                printMethods();
                return 0;
            }
        }

        cmd.getParam(1, opt_inDirectory);
        cmd.getParam(2, opt_anonymizedPrefix);

        OFLog::configureFromCommandLine(cmd, app);

        if (cmd.findOption("--fno-uid-root") &&
            cmd.findOption("--offis-uid-root") &&
            cmd.findOption("--custom-uid-root")) {
            checkConflict(app, "--fno-uid-root", "--offis-uid-root", "--custom-uid-root");
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
        if (cmd.findOption("--fno-uid-root")) opt_rootUID = FNO_UID_ROOT;
        if (cmd.findOption("--offis-uid-root")) opt_rootUID = OFFIS_UID_ROOT;
        if (cmd.findOption("--custom-uid-root")) app.checkValue(cmd.getValue(opt_rootUID));
        cmd.endOptionBlock();

        if (cmd.findOption("--out-directory")) {
            app.checkValue(cmd.getValue(opt_outDirectory));
        }

        if (cmd.findOption("--key-list")) {
            app.checkValue(cmd.getValue(opt_patientListFilename));
        }

        if (cmd.findOption("--filename-hex") && cmd.findOption("--filename-modality-sop")) {
            checkConflict(app, "--filename-hex", "--filename-modality-sop");
        }

        cmd.beginOptionBlock();
        if (cmd.findOption("--filename-hex")) opt_filenameType = F_HEX;
        if (cmd.findOption("--filename-modality-sop")) opt_filenameType = F_MODALITY_SOPINSTUID;
        cmd.endOptionBlock();

        if (cmd.findOption("--anonym-method", 0, OFCommandLine::FOM_FirstFromLeft)) {
            do {
                const char *method{nullptr};
                app.checkValue(cmd.getValue(method));
                if (method == "DCM_113108") opt_anonymizationMethods.insert(M_113108);
                if (method == "DCM_113109") opt_anonymizationMethods.insert(M_113109);
                if (method == "DCM_113112") opt_anonymizationMethods.insert(M_113112);
            } while (cmd.findOption("--anonym-method", 0, OFCommandLine::FOM_NextFromLeft));
        }

        OFLOG_DEBUG(mainLogger, rcsid.c_str() << OFendl);
    }

    if (std::filesystem::exists(opt_inDirectory)) {
        if (!std::filesystem::is_directory(opt_inDirectory)) {
            OFLOG_ERROR(mainLogger, fmt::format("Invalid path: <not directory> \"{}\"", opt_inDirectory).c_str());
            return EXITCODE_COMMANDLINE_SYNTAX_ERROR;
        }

        if (std::filesystem::is_empty(opt_inDirectory)) {
            OFLOG_ERROR(mainLogger, fmt::format("Invalid directory <empty directory> \"{}\"", opt_inDirectory).c_str());
            return EXITCODE_COMMANDLINE_SYNTAX_ERROR;
        }
    } else {
        OFLOG_ERROR(mainLogger, fmt::format("Invalid path <directory not found> \"{}\"", opt_inDirectory).c_str());
        return EXITCODE_COMMANDLINE_SYNTAX_ERROR;
    }

    StudyAnonymizer anonymizer{};

    // used to format leading zeros
    const int numberOfStudies = getNumberOfStudies(opt_inDirectory);
    const int indexWidth      = static_cast<int>(std::to_string(numberOfStudies).length());

    anonymizer.m_patientListFilename = opt_patientListFilename;
    anonymizer.m_filenameType        = opt_filenameType;

    (void) std::filesystem::create_directories(opt_outDirectory);
    OFLOG_INFO(mainLogger, fmt::format("Created output directory \"{}\"", opt_outDirectory));
    fmt::ostream keyListFile = fmt::output_file(fmt::format("{}/{}",
                                                            opt_outDirectory,
                                                            opt_patientListFilename));
    keyListFile.print("old name, old id, new name/id\n");

    int index{1};
    for (const auto &studyDir : std::filesystem::directory_iterator(opt_inDirectory)) {
        fmt::print("Anonymizing study {}\n", studyDir.path().stem().string());

        bool cond = anonymizer.getStudyFilenames(studyDir.path());
        if (!cond) {
            OFLOG_ERROR(mainLogger,
                        fmt::format("Failed to get study file names from: \"{}\"",
                            studyDir.path().stem().string()).c_str());
            continue;
        }

        const std::string pseudoname = fmt::format("{}{:0{}}",
                                                   opt_anonymizedPrefix,
                                                   index++,
                                                   indexWidth);
        OFLOG_INFO(mainLogger, "Applying pseudoname " << pseudoname);

        anonymizer.m_outputStudyDir = fmt::format("{}/{}/DATA", opt_outDirectory, pseudoname);
        
        if (std::filesystem::exists(anonymizer.m_outputStudyDir)) {
            OFLOG_INFO(mainLogger,
                       fmt::format("Directory \"{}\" exists, overwriting files",
                           anonymizer.m_outputStudyDir).c_str()
                      );
        } else {
            if (std::filesystem::create_directories(anonymizer.m_outputStudyDir))
                OFLOG_INFO(mainLogger,
                           fmt::format(R"(Created directory "{}"\n)", anonymizer.m_outputStudyDir).c_str()
                           );
        }

        cond = anonymizer.anonymizeStudy(pseudoname, opt_anonymizationMethods, opt_rootUID);

        // something bad happened
        if (!cond) {
            OFLOG_ERROR(mainLogger,
                        fmt::format(R"(Failed to anonymize study: "{}"\n)", studyDir.path().stem().string()).c_str()
                       );
            continue;
        }

        keyListFile.print("{},{},{}\n", anonymizer.m_oldName, anonymizer.m_oldID, pseudoname);
    }
    keyListFile.close();


    return 0;
}
