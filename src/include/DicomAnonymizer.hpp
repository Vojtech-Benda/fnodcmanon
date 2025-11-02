//
// Created by Vojtěch on 18.03.2025.
//

#ifndef DICOMANONYMIZER_HPP
#define DICOMANONYMIZER_HPP

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "dcmtk/dcmdata/dcdatset.h"
#include "dcmtk/dcmdata/dcfilefo.h"
#include "dcmtk/ofstd/ofcond.h"

extern OFLogger mainLogger;

void setupLogger(std::string_view logger_name);

enum E_FILENAMES { F_HEX, F_MODALITY_SOPINSTUID };

enum E_ADDIT_ANONYM_METHODS {
  // https://dicom.nema.org/medical/dicom/current/output/chtml/part16/chapter_D.html#DCM_113100
  M_113108, // Retain Patient Characteristics Option
  M_113109, // Retain Device Identity Option
  M_113112  // Retain Institution Identity Option
};

enum E_PSEUDONAME_TYPE { P_RANDOM_STRING, P_INTEGER_ORDER, P_FROM_FILE };

class StudyAnonymizer {
public:
  StudyAnonymizer() = default;
  StudyAnonymizer(const std::string &pseudoname_prefix,
                  E_PSEUDONAME_TYPE pseudoname_type = P_RANDOM_STRING)
      : m_pseudoname_prefix{pseudoname_prefix},
        m_pseudoname_type{pseudoname_type} {};

  ~StudyAnonymizer() = default;

  OFCondition findDicomFiles(const std::filesystem::path &study_directory);

  OFCondition anonymizeStudy(const std::filesystem::path &study_directory,
                             const std::string &output_directory,
                             const std::set<E_ADDIT_ANONYM_METHODS> &methods,
                             const std::string &uid_root);
  void anonymizeBasicProfile();
  void anonymizePatientCharacteristicsProfile();
  void anonymizeInstitutionProfile();
  void anonymizeDeviceProfile();
  void setPseudoname();

  std::string getSeriesUids(const std::string &old_series_uid,
                            const char *root = nullptr);

  OFCondition readPseudonamesFromFile(const std::string &filename);
  OFCondition removeInvalidTags() const;
  OFCondition setBasicTags();
  OFCondition writeDicomFile();
  OFCondition writeTags() const;

  E_FILENAMES m_filename_type{F_HEX};
  E_PSEUDONAME_TYPE m_pseudoname_type{P_RANDOM_STRING};
  unsigned int m_study_count{1};
  unsigned short m_count_width{2};
  std::string m_pseudoname_prefix{};

  std::string m_pseudoname{};
  std::string m_old_name{};
  std::string m_old_id{};
  std::string m_old_studyuid{};
  std::string m_new_studyuid{};
  std::string m_study_date{};
  std::string m_output_study_dir{};

private:
  unsigned int m_files_processed{0};
  std::vector<std::string> m_dicom_files{};
  std::unordered_map<std::string, std::string>
      m_series_uids{}; // unordered_map[old_uid, new_uid]
  std::unordered_map<std::string, std::string> m_id_pseudoname_map{};
  DcmFileFormat m_fileformat;
  DcmDataset *m_dataset{nullptr};
};

#endif // DICOMANONYMIZER_HPP
