//
// Created by Vojtěch on 18.03.2025.
//

#include <set>

#include "DicomAnonymizer.hpp"

#include "dcmtk/dcmdata/dcuid.h"
#include "dcmtk/oflog/oflog.h"

OFLogger mainLogger = OFLog::getLogger("");

void setupLogger(std::string_view logger_name) {
    mainLogger = OFLog::getLogger(logger_name.data());
    // mainLogger.setLogLevel(OFLogger::LogLevel::INFO_LOG_LEVEL);
};

bool StudyAnonymizer::getStudyFilenames(const std::filesystem::path &study_directory) {

    // remove previous filenames and series uids when iterating over new study directory
    if (!m_dicom_files.empty()) m_dicom_files.clear();
    if (!m_series_uids.empty()) m_series_uids.clear();

    for (const auto& entry: std::filesystem::recursive_directory_iterator(study_directory)) {
        if (entry.is_directory() || entry.path().filename() == "DICOMDIR") continue;

        m_dicom_files.push_back(entry.path().string());
    }

    if (m_dicom_files.empty()) {
        OFLOG_ERROR(mainLogger, "No files found");
        return false;
    }

    fmt::print("Found {} files\n", m_dicom_files.size());
    return true;
}

bool StudyAnonymizer::anonymizeStudy(const std::string                &pseudoname,
                                     const std::set<E_ANONYM_METHODS> &methods,
                                     const char *                      root) {


    char newStudyUID[65];
    dcmGenerateUniqueIdentifier(newStudyUID, root);

    int fileNumber{0};
    for (const auto& file : m_dicom_files) {
        OFCondition cond = m_fileformat.loadFile(file.c_str());
        if (cond.bad()) {
            OFLOG_ERROR(mainLogger, "Unable to load file " << file.c_str());
            OFLOG_ERROR(mainLogger, cond.text());
            return false;
        }

        m_dataset = m_fileformat.getDataset();
        // for writing pre-post deidentification to text file
        m_dataset->findAndGetOFString(DCM_PatientName, m_oldName);
        m_dataset->findAndGetOFString(DCM_PatientID, m_oldID);

        // dicom tags anonymization specification https://dicom.nema.org/medical/dicom/current/output/chtml/part15/chapter_E.html
        // deidentification methods explained https://dicom.nema.org/medical/dicom/current/output/chtml/part16/sect_CID_7050.html

        // Basic Application Confidentiality Profile
        if (methods.contains(M_113100)) {
            m_dataset->putAndInsertOFStringArray(DCM_PatientName, pseudoname.c_str());
            m_dataset->putAndInsertOFStringArray(DCM_PatientID, pseudoname.c_str());
            m_dataset->putAndInsertOFStringArray(DCM_PatientAddress, "");
            m_dataset->putAndInsertOFStringArray(DCM_AdditionalPatientHistory, "");
            m_dataset->remove(DCM_PatientInstitutionResidence);
            // m_dataset->remove(DCM_PatientAddress);
            // m_dataset->remove(DCM_AdditionalPatientHistory);

            // Retain Patient Characteristics Option
            // remove/replace tags if option not found
            if (!methods.contains(M_113108)) {
                // FIXME where applicable replace ->putAndInsertOFStringArray with ->remove
                m_dataset->putAndInsertOFStringArray(DCM_PatientAge, "000Y");
                m_dataset->putAndInsertOFStringArray(DCM_PatientSex, "O");
                // m_dataset->remove(DCM_PatientAge);
                // m_dataset->remove(DCM_PatientWeight);
                // m_dataset->remove(DCM_PatientSize);
            }
        }


        // Retain Institution Identity Option
        // remove/replace tags if option not found
        if (!methods.contains(M_113112)) {
            // FIXME where applicable replace ->putAndInsertOFStringArray with ->remove
            // institution tags
            m_dataset->putAndInsertOFStringArray(DCM_InstitutionName, "");
            m_dataset->putAndInsertOFStringArray(DCM_InstitutionAddress, "");
            m_dataset->putAndInsertOFStringArray(DCM_InstitutionalDepartmentName, "");

            // operator, physician and other medical personel tags
            m_dataset->putAndInsertOFStringArray(DCM_OperatorsName, "");
            m_dataset->putAndInsertOFStringArray(DCM_ReferringPhysicianName, "");
            m_dataset->putAndInsertOFStringArray(DCM_PerformingPhysicianName, "");
            m_dataset->putAndInsertOFStringArray(DCM_PhysiciansOfRecord, "");
            m_dataset->putAndInsertOFStringArray(DCM_NameOfPhysiciansReadingStudy, "");

            // m_dataset->remove(DCM_NameOfPhysiciansReadingStudy);
            // m_dataset->remove(DCM_PerformingPhysicianName);
            // m_dataset->remove(DCM_ScheduledPerformingPhysicianName);
            // m_dataset->remove(DCM_OperatorsName);
            // m_dataset->remove(DCM_InstitutionName);
            // m_dataset->remove(DCM_InstitutionAddress);
            // m_dataset->remove(DCM_InstitutionalDepartmentName);

        }


        // OFString oldSeriesUID{};
        const char *oldSeriesUID{nullptr};
        // m_dataset->findAndGetOFString(DCM_SeriesInstanceUID, oldSeriesUID);
        m_dataset->findAndGetString(DCM_SeriesInstanceUID, oldSeriesUID);
        // OFString newSeriesUID = this->getSeriesUids(oldSeriesUID, root).c_str();
        const std::string newSeriesUID = this->getSeriesUids(oldSeriesUID, root);
        // m_dataset->putAndInsertOFStringArray(DCM_SeriesInstanceUID, newSeriesUID);
        m_dataset->putAndInsertString(DCM_SeriesInstanceUID, newSeriesUID.c_str());

        char newSOPInstanceUID[65];
        dcmGenerateUniqueIdentifier(newSOPInstanceUID, root);
        m_dataset->putAndInsertOFStringArray(DCM_SOPInstanceUID, newSOPInstanceUID);

        m_dataset->putAndInsertOFStringArray(DCM_StudyInstanceUID, newStudyUID);

        if (!this->removeInvalidTags()) {
            OFLOG_ERROR(mainLogger, "Error occured while removing invalid tags");
            return false;
        }
        const E_TransferSyntax xfer = m_dataset->getCurrentXfer();
        m_dataset->chooseRepresentation(xfer, nullptr);
        m_fileformat.loadAllDataIntoMemory();

        std::string outputName{};
        if (m_filenameType == F_HEX) {
            outputName = fmt::format("{:08X}", fileNumber);
        } else if (m_filenameType == F_MODALITY_SOPINSTUID) {
            const char *modality;
            m_dataset->findAndGetString(DCM_Modality, modality);
            outputName = fmt::format("{}{}", modality, newSOPInstanceUID);
        }
        const std::string outputPath = fmt::format("{}/{}", m_outputStudyDir, outputName);
        cond = m_fileformat.saveFile(outputPath.c_str(), xfer);

        if (cond.bad()) {
            fmt::print("Unable to save file {}\n", file);
            fmt::print("Reason: {}\n", cond.text());
            return false;
        }
        ++fileNumber;
    }

    return true;
}

std::string StudyAnonymizer::getSeriesUids(const std::string &old_series_uid, const char* root) {

    // add old-new series uid map if there isn't one
    // otherwise return existing new uid
    if (!m_series_uids.contains(old_series_uid)) {
        char uid[65];
        dcmGenerateUniqueIdentifier(uid, root);
        m_series_uids[old_series_uid] = std::string(uid);
        return m_series_uids[old_series_uid];
    }

    return m_series_uids[old_series_uid];
}

bool StudyAnonymizer::removeInvalidTags() const {

    // sanity check
    if (m_dataset == nullptr) {
        OFLOG_ERROR(mainLogger, "Dataset is nullptr");
        return false;
    }

    for (unsigned long i = 0; i < m_dataset->card(); ++i) {
        const DcmElement *element = m_dataset->getElement(i);
        DcmTag tag = element->getTag();
        const DcmTagKey tagKey = DcmTagKey(element->getGTag(), element->getETag());
        const std::string tagName = tag.getTagName();
        if (tagName == "Unknown Tag & Data") {
            m_dataset->remove(tagKey);
            --i; // decrement due to ->remove reducing total number of tags
        }
    }

    return true;
}

StudySQLFields StudyAnonymizer::getPatientID() {
    OFCondition cond = m_fileformat.loadFile(m_dicom_files[0].c_str());
    if (cond.bad()) {
        OFLOG_ERROR(mainLogger, "Unable to load file " << m_dicom_files[0].c_str());
        OFLOG_ERROR(mainLogger, cond.text());
        return {};
    }

    DcmDataset *dataset = m_fileformat.getDataset();
    OFString patientID{}, studyInstanceUID{}, studyDate{}, modality{};
    (void)dataset->findAndGetOFString(DCM_PatientID, patientID);
    (void)dataset->findAndGetOFString(DCM_StudyInstanceUID, studyInstanceUID);
    (void)dataset->findAndGetOFString(DCM_StudyDate, studyDate);
    (void)dataset->findAndGetOFString(DCM_Modality, modality);
    m_fileformat.clear();
    return StudySQLFields(patientID.c_str(),
                          studyInstanceUID.c_str(),
                          studyDate.c_str(),
                          modality.c_str());
}