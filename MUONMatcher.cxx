#include "MUONMatcher.h"

Float_t EtaToTheta(Float_t arg);

//_________________________________________________________________________________________________
MUONMatcher::MUONMatcher() {

  const auto grp = o2::parameters::GRPObject::loadFrom("o2sim_grp.root");
  std::unique_ptr<o2::parameters::GRPObject> mGRP = nullptr;
  mGRP.reset(grp);
  o2::base::Propagator::initFieldFromGRP(grp);
  auto field = static_cast<o2::field::MagneticField *>(
      TGeoGlobalMagField::Instance()->GetField());

  double position[3] = {0, 0, -61.4};
  mField_z = field->getBz(position);
  printf("B field z = %f [kGauss]\n", mField_z);

  mMCHTrackExtrap.setField();
  mCutFunc = &MUONMatcher::matchCutDisabled;
  mMatchFunc = &MUONMatcher::matchMFT_MCH_TracksAllParam;
}

//_________________________________________________________________________________________________
void MUONMatcher::LoadAbsorber() {

  if (!mGeoManager) {
    mGeoManager = new TGeoManager("Matcher Geometry", "Matcher Geometry");
    o2::passive::Cave("CAVE", "Cave").ConstructGeometry();
    o2::passive::Shil("SHIL", "Small angle beam shield").ConstructGeometry();
    o2::passive::Absorber("ABSO", "Absorber").ConstructGeometry();
    mGeoManager->CloseGeometry();
  }
}

//_________________________________________________________________________________________________
void MUONMatcher::Clear() {

  mMFTTracks.clear();
  mMCHTracksDummy.clear();
  mMCHTracks.clear();
  mGlobalMuonTracks.clear();
  mMFTClusters.clear();
  mtrackExtClsIDs.clear();
  mftTrackLabels.clear();
  mchTrackLabels.clear();
  mGlobalTrackLabels.clear();
}

//_________________________________________________________________________________________________
void MUONMatcher::loadMCHTracks() {
  // This function populates mMCHTracks (vector of MCH tracks)
  //

  // For now loading MCH Tracks
  std::string trkFile = "tempMCHTracks.root";
  TFile *trkFileIn = new TFile(trkFile.c_str());
  TTree *mchTrackTree = (TTree *)trkFileIn->Get("treeMCH");
  std::vector<tempMCHTrack> trackMCHVec, *trackMCHVecP = &trackMCHVec;
  mchTrackTree->SetBranchAddress("tempMCHTracks", &trackMCHVecP);
  mNEvents = mchTrackTree->GetEntries();
  mMCHTracks.clear();
  mchTrackLabels.clear();

  auto MCHTrkID = 0;

  for (int event = 0; event < mNEvents; event++) {
    mchTrackTree->GetEntry(event);

    for (auto &mchtrackIn : trackMCHVec) {
      MCHTrack mchTrackOut;
      mchTrackOut.setZ(mchtrackIn.fZ);
      mchTrackOut.setNonBendingCoor(mchtrackIn.fNonBendingCoor);
      mchTrackOut.setNonBendingSlope(mchtrackIn.fThetaX);
      mchTrackOut.setBendingCoor(mchtrackIn.fBendingCoor);
      mchTrackOut.setBendingSlope(mchtrackIn.fThetaY);
      mchTrackOut.setInverseBendingMomentum(mchtrackIn.fInverseBendingMomentum);
      TMatrixD cov(5, 5);
      cov(0, 0) = mchtrackIn.fCovariances[0];
      cov(0, 1) = mchtrackIn.fCovariances[1];
      cov(0, 2) = mchtrackIn.fCovariances[3];
      cov(0, 3) = mchtrackIn.fCovariances[6];
      cov(0, 4) = mchtrackIn.fCovariances[10];

      cov(1, 1) = mchtrackIn.fCovariances[2];
      cov(1, 2) = mchtrackIn.fCovariances[4];
      cov(1, 3) = mchtrackIn.fCovariances[7];
      cov(1, 4) = mchtrackIn.fCovariances[11];

      cov(2, 2) = mchtrackIn.fCovariances[5];
      cov(2, 3) = mchtrackIn.fCovariances[8];
      cov(2, 4) = mchtrackIn.fCovariances[12];

      cov(3, 3) = mchtrackIn.fCovariances[9];
      cov(3, 4) = mchtrackIn.fCovariances[13];

      cov(4, 4) = mchtrackIn.fCovariances[14];

      cov(1, 0) = cov(0, 1);
      cov(2, 0) = cov(0, 2);
      cov(3, 0) = cov(0, 3);
      cov(4, 0) = cov(0, 4);

      cov(2, 1) = cov(1, 2);
      cov(3, 1) = cov(1, 3);
      cov(4, 1) = cov(1, 4);

      cov(3, 2) = cov(2, 3);
      cov(4, 2) = cov(2, 4);

      cov(4, 3) = cov(3, 4);

      mchTrackOut.setCovariances(cov);

      if (mVerbose)
        std::cout << " Loading MCH Track # " << MCHTrkID
                  << "; Label = " << mchtrackIn.fLabel << " in event "
                  << mchtrackIn.fiEv << std::endl;

      if (mchtrackIn.fiEv > mNEvents)
        mNEvents = mchtrackIn.fiEv;

      o2::MCCompLabel thisLabel(mchtrackIn.fLabel, mchtrackIn.fiEv, 0,
                                0); // FIXME: srcID
      mchTrackLabels.addElement(MCHTrkID, thisLabel);
      mMCHTracks.push_back(mchTrackOut);
      MCHTrkID++;
    }
  }

  std::cout << "Loaded " << mMCHTracks.size() << " MCH Tracks in " << mNEvents
            << " events" << std::endl;

  std::ifstream genConfigFile("MatcherGenConfig.txt");
  if (genConfigFile) {
    std::getline(genConfigFile, mMatchingHelper.Generator);
    std::cout << "Generator: " << mMatchingHelper.Generator << std::endl;
  }
}

//_________________________________________________________________________________________________
void MUONMatcher::loadDummyMCHTracks() {

  // For now loading MFT Tracks as Dummy MCH tracks
  std::string trkFile = "mfttracks.root";
  TFile *trkFileIn = new TFile(trkFile.c_str());
  TTree *mftTrackTree = (TTree *)trkFileIn->Get("o2sim");
  std::vector<o2::mft::TrackMFT> trackMFTVec, *trackMFTVecP = &trackMFTVec;
  mftTrackTree->SetBranchAddress("MFTTrack", &trackMFTVecP);

  o2::dataformats::MCTruthContainer<o2::MCCompLabel> *mcLabels = nullptr;
  mftTrackTree->SetBranchAddress("MFTTrackMCTruth", &mcLabels);

  mftTrackTree->GetEntry(0);
  mchTrackLabels = *mcLabels;
  mMCHTracksDummy.swap(trackMFTVec);
  std::cout << "Loaded " << mMCHTracksDummy.size() << " Fake MCH Tracks"
            << std::endl;
}

//_________________________________________________________________________________________________
void MUONMatcher::loadMFTTracksOut() {
  // Load all MFTTracks and propagate to last MFT Layer;

  std::string trkFile = "mfttracks.root";
  TFile *trkFileIn = new TFile(trkFile.c_str());
  TTree *mftTrackTree = (TTree *)trkFileIn->Get("o2sim");
  std::vector<o2::mft::TrackMFT> trackMFTVec, *trackMFTVecP = &trackMFTVec;
  mftTrackTree->SetBranchAddress("MFTTrack", &trackMFTVecP);

  std::vector<int> trackExtClsVec, *trackExtClsVecP = &trackExtClsVec;
  mftTrackTree->SetBranchAddress("MFTTrackClusIdx", &trackExtClsVecP);

  o2::dataformats::MCTruthContainer<o2::MCCompLabel> *mcLabels = nullptr;
  mftTrackTree->SetBranchAddress("MFTTrackMCTruth", &mcLabels);
  std::vector<o2::itsmft::ROFRecord> *mMFTTracksROFsP = nullptr;
  mftTrackTree->SetBranchAddress("MFTTracksROF", &mMFTTracksROFsP);
  mftTrackTree->GetEntry(0);
  mftTrackLabels = *mcLabels;
  mMFTTracks.swap(trackMFTVec);
  mtrackExtClsIDs.swap(trackExtClsVec);
  mMFTTracksROFs.swap(*mMFTTracksROFsP);

  std::cout << "Loaded " << mMFTTracks.size()
            << " MFT Tracks. Label info:" << std::endl;
  mcLabels->print(std::cout);

  for (auto &track : mMFTTracks) {
    track.setParameters(track.getOutParam().getParameters());
    track.setCovariances(track.getOutParam().getCovariances());
    track.setZ(track.getOutParam().getZ());
    track.propagateToZhelix(mMatchingPlaneZ, mField_z);
  }

  loadMFTClusters();
}

//_________________________________________________________________________________________________
void MUONMatcher::loadMFTClusters() {

  using o2::itsmft::CompClusterExt;

  constexpr float DefClusErrorRow = 26.88e-4 * 0.5;
  constexpr float DefClusErrorCol = 29.24e-4 * 0.5;
  constexpr float DefClusError2Row = DefClusErrorRow * DefClusErrorRow;
  constexpr float DefClusError2Col = DefClusErrorCol * DefClusErrorCol;

  // Geometry and matrix transformations
  std::string inputGeom = "o2sim_geometry.root";
  o2::base::GeometryManager::loadGeometry(inputGeom);
  auto gman = o2::mft::GeometryTGeo::Instance();
  gman->fillMatrixCache(
      o2::math_utils::bit2Mask(o2::math_utils::TransformType::L2G));

  // Cluster pattern dictionary
  std::string dictfile = "MFTdictionary.bin";
  o2::itsmft::TopologyDictionary dict;
  std::ifstream file(dictfile.c_str());
  if (file.good()) {
    printf("Running with dictionary: %s \n", dictfile.c_str());
    dict.readBinaryFile(dictfile);
  } else {
    printf("Can not run without dictionary !\n");
    return;
  }

  // Clusters

  TFile fileC("mftclusters.root");
  TTree *clsTree = (TTree *)fileC.Get("o2sim");
  std::vector<CompClusterExt> clsVec, *clsVecP = &clsVec;
  clsTree->SetBranchAddress("MFTClusterComp", &clsVecP);
  o2::dataformats::MCTruthContainer<o2::MCCompLabel> *clsLabels = nullptr;
  if (clsTree->GetBranch("MFTClusterMCTruth")) {
    clsTree->SetBranchAddress("MFTClusterMCTruth", &clsLabels);
  } else {
    printf("No Monte-Carlo information in this file\n");
    return;
  }

  int nEntries = clsTree->GetEntries();
  printf("Number of entries in clusters tree %d \n", nEntries);

  clsTree->GetEntry(0);

  int nClusters = clsVec.size();
  printf("Number of clusters %d \n", nClusters);

  auto clusterId = 0;
  for (auto &c : clsVec) {
    auto chipID = c.getChipID();
    auto pattID = c.getPatternID();
    o2::math_utils::Point3D<float> locC;
    float sigmaX2 = DefClusError2Row, sigmaY2 = DefClusError2Col;

    if (pattID != o2::itsmft::CompCluster::InvalidPatternID) {
      // sigmaX2 = dict.getErr2X(pattID); // ALPIDE local X coordinate => MFT
      // global X coordinate (ALPIDE rows) sigmaY2 = dict.getErr2Z(pattID); //
      // ALPIDE local Z coordinate => MFT global Y coordinate (ALPIDE columns)
      // temporary, until ITS bug fix
      sigmaX2 = dict.getErrX(pattID) * dict.getErrX(pattID);
      sigmaY2 = dict.getErrZ(pattID) * dict.getErrZ(pattID);
      if (!dict.isGroup(pattID)) {
        locC = dict.getClusterCoordinates(c);
      } else {
        locC = dict.getClusterCoordinates(c);
      }
    } else {
      locC = dict.getClusterCoordinates(c);
    }

    // Transformation to the local --> global
    auto gloC = gman->getMatrixL2G(chipID) * locC;
    // printf("Cluster %5d   chip ID %03d   evn %2d   mctrk %4d   x,y,z  %7.3f
    // %7.3f  %7.3f \n", icls, cluster.getChipID(), evnID, trkID, gloC.X(),
    // gloC.Y(), gloC.Z());

    auto clsPoint2D = o2::math_utils::Point2D<Float_t>(gloC.x(), gloC.y());
    Float_t rCoord = clsPoint2D.R();
    Float_t phiCoord = clsPoint2D.Phi();
    o2::math_utils::bringTo02PiGen(phiCoord);
    int rBinIndex = 0;   // constants::index_table::getRBinIndex(rCoord);
    int phiBinIndex = 0; // constants::index_table::getPhiBinIndex(phiCoord);
    int binIndex =
        0; // constants::index_table::getBinIndex(rBinIndex, phiBinIndex);
    MFTCluster &thisCluster = mMFTClusters.emplace_back(
        gloC.x(), gloC.y(), gloC.z(), phiCoord, rCoord, clusterId, binIndex,
        sigmaX2, sigmaY2, chipID);
    clusterId++;
  }
}

//_________________________________________________________________________________________________
void MUONMatcher::initGlobalTracks() {
  // Populates mGlobalMuonTracks using MCH track data

  if (!mGeoManager) {
    mGeoManager = new TGeoManager("Matcher Geometry", "Matcher Geometry");
    o2::passive::Cave("CAVE", "Cave").ConstructGeometry();
    o2::passive::Shil("SHIL", "Small angle beam shield").ConstructGeometry();
    o2::passive::Absorber("ABSO", "Absorber").ConstructGeometry();
    mGeoManager->CloseGeometry();
  }

  mGlobalMuonTracks.clear();
  for (auto &track : mMCHTracks) {
    // mMCHTrackExtrap.extrapToVertex(&track,mMatchingPlaneZ); // No corrections
    mMCHTrackExtrap.extrapToMatchingPlane(&track, mMatchingPlaneZ);
    mGlobalMuonTracks.push_back(MCHtoGlobal(track));
  }
  mMCHTracks.clear();

  // Populates mMatchingHelper.MatchingCutConfig with mCutParams
  MatchingHelper &helper = mMatchingHelper;
  helper.matchingPlaneZ = mMatchingPlaneZ;

  auto iparam = 0;
  mMatchingHelper.MatchingCutConfig = "";
  for (auto param : mCutParams) {
    mMatchingHelper.MatchingCutConfig = mMatchingHelper.MatchingCutConfig +
                                        "_CutP" + std::to_string(iparam) + "=" +
                                        std::to_string(param);
    iparam++;
  }
  if (mMatchingHelper.MatchingFunction == "") {
    if (mMatchFunc == &MUONMatcher::matchMFT_MCH_TracksXY)
      mMatchingHelper.MatchingFunction = "_matchXY";
    if (mMatchFunc == &MUONMatcher::matchMFT_MCH_TracksXYPhiTanl)
      mMatchingHelper.MatchingFunction = "_matchXYPhiTanl";
    if (mMatchFunc == &MUONMatcher::matchMFT_MCH_TracksAllParam)
      mMatchingHelper.MatchingFunction = "_matchAllParams";
    if (mMatchFunc == &MUONMatcher::Hiroshima)
      mMatchingHelper.MatchingFunction = "_Hiroshima";
  }

  if (mMatchingHelper.MatchingCutFunc == "") {
    if (mCutFunc == &MUONMatcher::matchCutDisabled)
      helper.MatchingCutFunc = "_cutDisabled";
    if (mCutFunc == &MUONMatcher::matchCutDistance)
      helper.MatchingCutFunc = "_cutDistance";
    if (mCutFunc == &MUONMatcher::matchCutDistanceSigma)
      helper.MatchingCutFunc = "_cutDistanceSigma";
    if (mCutFunc == &MUONMatcher::matchCutDistanceAndAngles)
      helper.MatchingCutFunc = "_cutDistanceAndAngles";
    if (mCutFunc == &MUONMatcher::matchCut3SigmaXYAngles)
      helper.MatchingCutFunc = "_cutDistanceAndAngles3Sigma";
  }
}

//_________________________________________________________________________________________________
void MUONMatcher::initDummyGlobalTracks() {
  // Populates mGlobalMuonTracks using MFT track inner parameters

  if (mGlobalMuonTracks.empty()) {
    for (auto &track : mMCHTracksDummy) { // Running on dummy MCH tracks while
                                          // MCH Tracks are not loaded
      GlobalMuonTrack gTrack;
      gTrack.setParameters(track.getParameters());
      gTrack.setCovariances(track.getCovariances());
      gTrack.setZ(track.getZ());
      gTrack.propagateToZhelix(mMatchingPlaneZ, mField_z);
      mGlobalMuonTracks.push_back(gTrack);
    }
  } else
    std::cout << "WARNING: mGlobalMuonTracks already initialized! Skipping "
                 "initDummyGlobalTracks()";
}

//_________________________________________________________________________________________________
void MUONMatcher::runEventMatching() {
  // Runs matching over all tracks on a single event
  std::cout << "mGlobalMuonTracks.size() = " << mGlobalMuonTracks.size()
            << std::endl;
  std::cout << "Annotation: " << mMatchingHelper.Annotation() << std::endl;
  std::cout << "Running runEventMatching for " << mNEvents << " events"
            << std::endl;

  uint32_t GTrackID = 0;

  for (int event = 0; event < mNEvents; event++) {

    // std::cout << "Matching event # " << event << std::endl;
    GTrackID = 0;

    for (auto &gTrack : mGlobalMuonTracks) {
      auto MCHlabel = mchTrackLabels.getLabels(GTrackID);
      if (MCHlabel[0].getEventID() == event) {
        auto mftTrackID = 0;
        if (mCustomMatchFunc) { // Custom matching function
          for (auto mftTrack : mMFTTracks) {
            auto MFTlabel = mftTrackLabels.getLabels(mftTrackID);
            if (mftTrack.getCharge() == gTrack.getCharge())
              if (MFTlabel[0].getEventID() == event)
                if (matchingCut(gTrack, mftTrack)) {
                  gTrack.countCandidate();
                  if (MFTlabel[0].getTrackID() == MCHlabel[0].getTrackID())
                    gTrack.setCloseMatch();
                  auto chi2 = (*mCustomMatchFunc)(gTrack, mftTrack);
                  if (chi2 < gTrack.getMatchingChi2()) {
                    gTrack.setBestMFTTrackMatchID(mftTrackID);
                    gTrack.setMatchingChi2(chi2);
                  }
                }
            mftTrackID++;
          }
        } else { // Built-in matching function
          for (auto mftTrack : mMFTTracks) {
            auto MFTlabel = mftTrackLabels.getLabels(mftTrackID);
            if (mftTrack.getCharge() == gTrack.getCharge())
              if (MFTlabel[0].getEventID() == event)
                if (matchingCut(gTrack, mftTrack)) {
                  gTrack.countCandidate();
                  if (MFTlabel[0].getTrackID() == MCHlabel[0].getTrackID())
                    gTrack.setCloseMatch();
                  auto chi2 = (this->*mMatchFunc)(gTrack, mftTrack);
                  if (chi2 < gTrack.getMatchingChi2()) {
                    gTrack.setBestMFTTrackMatchID(mftTrackID);
                    gTrack.setMatchingChi2(chi2);
                  }
                }
            mftTrackID++;
          }
        }
      }
      GTrackID++;
    } // /loop over global tracks
  }   // /loop over events

  finalize();
  std::cout << "Finished runEventMatching on " << GTrackID << " MCH Tracks."
            << std::endl;
}

//_________________________________________________________________________________________________
void MUONMatcher::printMatchingPlaneView(int MCHTrackID) {

  if (MCHTrackID > mGlobalMuonTracks.size()) {
    std::cout << "  printMatchingPlaneView out of range: MCH Track ID = "
              << MCHTrackID << "  ; nMCHTracks = " << mGlobalMuonTracks.size()
              << std::endl;
    return;
  }

  auto MCHlabel = mchTrackLabels.getLabels(MCHTrackID);
  auto event = MCHlabel[0].getEventID();
  auto MCHTrack = mGlobalMuonTracks[MCHTrackID];

  auto localBestMFTTrack = -1;
  auto localCorrectMFTMatch = -1;

  std::vector<double> xPositions;
  std::vector<double> yPositions;
  std::vector<std::string> pointsColors;

  xPositions.emplace_back(MCHTrack.getX());
  yPositions.emplace_back(MCHTrack.getY());
  pointsColors.emplace_back("orange");
  std::string matchParamStr, correctParamStr, mchParamStr, matchCovStr,
      correctCovStr, mchCovStr;
  mchParamStr = getParamString(MCHTrack);
  mchCovStr = getCovString(MCHTrack);

  auto mftTrackID = 0, nMFTTracks = 0;
  for (auto mftTrack : mMFTTracks) {
    auto MFTlabel = mftTrackLabels.getLabels(mftTrackID);
    if (MFTlabel[0].getEventID() == event) {
      nMFTTracks++;
      xPositions.emplace_back(mftTrack.getX());
      yPositions.emplace_back(mftTrack.getY());
      pointsColors.emplace_back("black");
      if ((mftTrack.getCharge() == MCHTrack.getCharge()) and
          matchingCut(MCHTrack, mftTrack)) {
        pointsColors.back() = "blue";
        MCHTrack.countCandidate();
        if (MFTlabel[0].getTrackID() == MCHlabel[0].getTrackID()) {
          MCHTrack.setCloseMatch();
          pointsColors.back() = "magenta";
          localCorrectMFTMatch = pointsColors.size();
          correctParamStr = getParamString(mftTrack);
          correctCovStr = getCovString(mftTrack);
        }
        auto chi2 = (this->*mMatchFunc)(MCHTrack, mftTrack);
        if (chi2 < MCHTrack.getMatchingChi2()) {
          MCHTrack.setBestMFTTrackMatchID(mftTrackID);
          MCHTrack.setMatchingChi2(chi2);
          localBestMFTTrack = pointsColors.size();
          matchParamStr = getParamString(mftTrack);
          matchCovStr = getCovString(mftTrack);
        }
      } else {
        if (MFTlabel[0].getTrackID() == MCHlabel[0].getTrackID()) {
          pointsColors.back() = "violet";
          correctParamStr = getParamString(mftTrack);
          correctCovStr = getCovString(mftTrack);
        }
      }
    }
    mftTrackID++;
  }

  if (localBestMFTTrack > -1) {
    pointsColors[localBestMFTTrack - 1] =
        (localCorrectMFTMatch == localBestMFTTrack) ? "green" : "red";
  }

  std::vector<double> xPositionsBlack, yPositionsBlack, xPositionsBlue,
      yPositionsBlue, xPositionsGreen, yPositionsGreen, xPositionsRed,
      yPositionsRed, xPositionsOrange, yPositionsOrange, xPositionsMagenta,
      yPositionsMagenta, xPositionsViolet, yPositionsViolet;

  if (mVerbose) {
    std::cout << " printMatchingPlaneView: " << std::endl;
  }

  for (int i = 0; i < pointsColors.size(); i++) {
    auto x = xPositions[i];
    auto y = yPositions[i];
    auto color = pointsColors[i];

    if (mVerbose) {
      std::cout << " x = " << x << " y = " << y;
      std::cout << " color = " << color << std::endl;
    }

    if (color == "blue") { // Candidate
      xPositionsBlue.emplace_back(x);
      yPositionsBlue.emplace_back(y);
    }

    if (color == "red") { // Fake MFT Track
      xPositionsRed.emplace_back(x);
      yPositionsRed.emplace_back(y);
    }

    if (color == "black") { // Excluded by cut function
      xPositionsBlack.emplace_back(x);
      yPositionsBlack.emplace_back(y);
    }

    if (color == "orange") { // MCH track
      xPositionsOrange.emplace_back(x);
      yPositionsOrange.emplace_back(y);
    }

    if (color == "green") { // Correct MFT match
      xPositionsGreen.emplace_back(x);
      yPositionsGreen.emplace_back(y);
    }

    if (color == "magenta") { // Missed
      xPositionsMagenta.emplace_back(x);
      yPositionsMagenta.emplace_back(y);
    }

    if (color == "violet") {
      xPositionsViolet.emplace_back(x);
      yPositionsViolet.emplace_back(y);
    }
  }

  TCanvas *canvasMatchingPlane =
      new TCanvas(("cMatchingPlane" + std::to_string(MCHTrackID)).c_str(),
                  "Matching Plane View", 1000, 1000);
  canvasMatchingPlane->SetFillStyle(4000);
  canvasMatchingPlane->SetFrameLineWidth(3);
  TMultiGraph *MultiGraph_MatchingPlane = new TMultiGraph();
  MultiGraph_MatchingPlane->SetName("MatchingPlaneView");

  auto legend = new TLegend(0.1, 0.73, 0.3, 0.9);
  legend->SetFillColorAlpha(kWhite, 0.);
  legend->SetBorderSize(2);

  TPaveText *pt;

  if (xPositionsBlack.size()) {
    TGraph *TGBlack = new TGraph(xPositionsBlack.size(), &xPositionsBlack[0],
                                 &yPositionsBlack[0]);
    TGBlack->SetTitle("No candidate");
    TGBlack->SetName("Not_tested");
    TGBlack->GetXaxis()->SetTitle("x");
    TGBlack->SetMarkerStyle(kFullCircle);
    TGBlack->SetMarkerColor(kBlack);
    TGBlack->SetMarkerSize(2);
    TGBlack->SetLineWidth(0);
    MultiGraph_MatchingPlane->Add(TGBlack);
    TGBlack->Draw();
  }

  if (xPositionsBlue.size()) {
    TGraph *TGBlue = new TGraph(xPositionsBlue.size(), &xPositionsBlue[0],
                                &yPositionsBlue[0]);
    TGBlue->SetTitle("Candidate");
    TGBlue->SetName("Candidate");
    TGBlue->GetXaxis()->SetTitle("x");
    TGBlue->SetMarkerStyle(kFullCircle);
    TGBlue->SetMarkerColor(kBlue);
    TGBlue->SetMarkerSize(2);
    TGBlue->SetLineWidth(0);
    MultiGraph_MatchingPlane->Add(TGBlue);
    legend->AddEntry(TGBlue);
  }

  if (xPositionsRed.size()) {
    TGraph *TGRed =
        new TGraph(xPositionsRed.size(), &xPositionsRed[0], &yPositionsRed[0]);
    TGRed->SetTitle("Fake Match");
    TGRed->SetName("FakeMatch");
    TGRed->GetXaxis()->SetTitle("x");
    TGRed->SetMarkerStyle(kFullCircle);
    TGRed->SetMarkerColor(kRed);
    TGRed->SetMarkerSize(3);
    TGRed->SetLineWidth(0);
    MultiGraph_MatchingPlane->Add(TGRed);
    legend->AddEntry(TGRed);
  }

  if (xPositionsGreen.size()) {
    TGraph *TGGreen = new TGraph(xPositionsGreen.size(), &xPositionsGreen[0],
                                 &yPositionsGreen[0]);
    TGGreen->SetTitle("CorrectMatch");
    TGGreen->SetName("CorrectMatch");
    TGGreen->GetXaxis()->SetTitle("x");
    TGGreen->SetMarkerStyle(kFullCircle);
    TGGreen->SetMarkerColor(kGreen);
    TGGreen->SetMarkerSize(3);
    TGGreen->SetLineWidth(0);
    MultiGraph_MatchingPlane->Add(TGGreen);
    legend->AddEntry(TGGreen);
  }

  if (xPositionsMagenta.size()) {
    TGraph *TGMagenta = new TGraph(
        xPositionsMagenta.size(), &xPositionsMagenta[0], &yPositionsMagenta[0]);
    TGMagenta->SetTitle("Close Match");
    TGMagenta->SetName("CloseMatch");
    TGMagenta->GetXaxis()->SetTitle("x");
    TGMagenta->SetMarkerStyle(kFullCircle);
    TGMagenta->SetMarkerColor(kMagenta);
    TGMagenta->SetMarkerSize(3);
    TGMagenta->SetLineWidth(0);
    MultiGraph_MatchingPlane->Add(TGMagenta);
    legend->AddEntry(TGMagenta);
  }

  if (xPositionsViolet.size()) {
    TGraph *TGViolet = new TGraph(xPositionsViolet.size(), &xPositionsViolet[0],
                                  &yPositionsViolet[0]);
    TGViolet->SetTitle("Far Match (missed)");
    TGViolet->SetName("FarMatch");
    TGViolet->GetXaxis()->SetTitle("x");
    TGViolet->SetMarkerStyle(kFullSquare);
    TGViolet->SetMarkerColor(kViolet);
    TGViolet->SetMarkerSize(2);
    TGViolet->SetLineWidth(0);
    MultiGraph_MatchingPlane->Add(TGViolet);
    legend->AddEntry(TGViolet);
  }

  TGraph *TGOrange = new TGraph(xPositionsOrange.size(), &xPositionsOrange[0],
                                &yPositionsOrange[0]);
  TGOrange->SetTitle("MCHTrack");
  TGOrange->SetName("MCHTrack");
  TGOrange->GetXaxis()->SetTitle("x");
  TGOrange->SetMarkerStyle(kFullCircle);
  TGOrange->SetMarkerColor(kOrange);
  TGOrange->SetMarkerSize(3);
  TGOrange->SetLineWidth(0);
  TGOrange->SetFillColorAlpha(kWhite, 0.);
  MultiGraph_MatchingPlane->Add(TGOrange);
  legend->AddEntry(TGOrange);

  MultiGraph_MatchingPlane->GetXaxis()->SetTitle("x [cm]");
  MultiGraph_MatchingPlane->GetYaxis()->SetTitle("y [cm]");
  MultiGraph_MatchingPlane->GetYaxis()->SetTitleOffset(1.25);
  MultiGraph_MatchingPlane->SetTitle("MatchingPlaneView");

  auto rOuterMatchingPlane = TMath::Abs(mMatchingPlaneZ * 14.35 / -77.5);
  auto rInnerMatchingPlane = TMath::Abs(mMatchingPlaneZ * 3.9 / -77.5);
  auto rMargin = 1.4;
  MultiGraph_MatchingPlane->GetXaxis()->SetLimits(
      -rMargin * rOuterMatchingPlane, rMargin * rOuterMatchingPlane);
  MultiGraph_MatchingPlane->SetMinimum(-rMargin * rOuterMatchingPlane);
  MultiGraph_MatchingPlane->SetMaximum(rMargin * rOuterMatchingPlane);
  MultiGraph_MatchingPlane->GetYaxis()->SetLimits(
      -rMargin * rOuterMatchingPlane, rMargin * rOuterMatchingPlane);
  gPad->Modified();
  MultiGraph_MatchingPlane->Draw("LP same");

  legend->Draw();
  canvasMatchingPlane->Update();
  TEllipse *outerR =
      new TEllipse(0, 0, rOuterMatchingPlane, rOuterMatchingPlane);
  outerR->SetLineWidth(2);
  outerR->SetFillColorAlpha(kWhite, 0.);
  outerR->Draw();

  TEllipse *innerR =
      new TEllipse(0, 0, rInnerMatchingPlane, rInnerMatchingPlane);
  innerR->SetLineWidth(2);
  innerR->SetFillColorAlpha(kWhite, 0.);
  innerR->Draw();

  pt = new TPaveText(0.1, 0.918, 0.9, 0.995, "NDC");
  pt->SetBorderSize(0);
  pt->SetFillStyle(4000);
  pt->AddText(
      ("Matching Plane View - MCH Track" + std::to_string(MCHTrackID)).c_str());
  pt->AddText(mMatchingHelper.Annotation().c_str());
  pt->Draw();

  pt = new TPaveText(0.3, 0.8, 0.9, 0.9, "NDC");
  pt->SetBorderSize(2);
  pt->SetFillStyle(4000);
  pt->AddText(("MCH: " + mchParamStr).c_str());
  pt->AddText((mchCovStr).c_str());

  if (xPositionsRed.size()) {
    pt->AddText(("Fake: " + matchParamStr).c_str());
    pt->AddText((matchCovStr).c_str());
  }

  if (xPositionsGreen.size()) {
    pt->AddText(("Correct: " + matchParamStr).c_str());
    pt->AddText((matchCovStr).c_str());
  }

  if (xPositionsMagenta.size()) {
    pt->AddText(("Close match: " + correctParamStr).c_str());
    pt->AddText((correctCovStr).c_str());
  }

  if (xPositionsViolet.size()) {
    pt->AddText(("Far match: " + correctParamStr).c_str());
    pt->AddText((correctCovStr).c_str());
  }
  pt->Draw();

  pt = new TPaveText(0.11, 0.12, 0.4, 0.15, "NDC");
  pt->SetBorderSize(0);
  pt->SetFillStyle(4000);
  pt->AddText(("nMFTTracks = " + std::to_string(nMFTTracks)).c_str());
  pt->Draw();

  canvasMatchingPlane->Draw();
  canvasMatchingPlane->SaveAs(
      ("MatchingPlaneMCHTrack" + std::to_string(MCHTrackID) + ".png").c_str());
}

//_________________________________________________________________________________________________
bool MUONMatcher::matchingCut(const GlobalMuonTrack &mchTrack,
                              const MFTTrack &mftTrack) {

  if (mCustomCutFunc) {
    return (*mCustomCutFunc)(mchTrack, mftTrack);
  }

  return (this->*mCutFunc)(mchTrack, mftTrack);
}

//_________________________________________________________________________________________________
bool MUONMatcher::matchCutDistance(const GlobalMuonTrack &mchTrack,
                                   const MFTTrack &mftTrack) {

  auto dx = mchTrack.getX() - mftTrack.getX();
  auto dy = mchTrack.getY() - mftTrack.getY();
  auto distance = TMath::Sqrt(dx * dx + dy * dy);
  return distance < mCutParams[0];
}

//_________________________________________________________________________________________________
bool MUONMatcher::matchCutDistanceAndAngles(const GlobalMuonTrack &mchTrack,
                                            const MFTTrack &mftTrack) {

  auto dx = mchTrack.getX() - mftTrack.getX();
  auto dy = mchTrack.getY() - mftTrack.getY();
  auto dPhi = TMath::Abs(mchTrack.getPhi() - mftTrack.getPhi());
  auto dTheta =
      TMath::Abs(EtaToTheta(mchTrack.getEta()) - EtaToTheta(mftTrack.getEta()));
  auto distance = TMath::Sqrt(dx * dx + dy * dy);
  return (distance < mCutParams[0]) and (dPhi < mCutParams[1]) and
         (dTheta < mCutParams[2]);
}

//_________________________________________________________________________________________________
bool MUONMatcher::matchCutDistanceSigma(const GlobalMuonTrack &mchTrack,
                                        const MFTTrack &mftTrack) {

  auto dx = mchTrack.getX() - mftTrack.getX();
  auto dy = mchTrack.getY() - mftTrack.getY();
  auto distance = TMath::Sqrt(dx * dx + dy * dy);
  auto cutDistance = mCutParams[0] *
                     TMath::Sqrt(mchTrack.getSigma2X() + mchTrack.getSigma2Y());
  return distance < cutDistance;
}

//_________________________________________________________________________________________________
bool MUONMatcher::matchCut3SigmaXYAngles(const GlobalMuonTrack &mchTrack,
                                         const MFTTrack &mftTrack) {

  auto dx = mchTrack.getX() - mftTrack.getX();
  auto dy = mchTrack.getY() - mftTrack.getY();
  auto dPhi = mchTrack.getPhi() - mftTrack.getPhi();
  auto dTheta =
      TMath::Abs(EtaToTheta(mchTrack.getEta()) - EtaToTheta(mftTrack.getEta()));
  auto distance = TMath::Sqrt(dx * dx + dy * dy);
  auto cutDistance =
      3 * TMath::Sqrt(mchTrack.getSigma2X() + mchTrack.getSigma2Y());
  auto cutPhi = 3 * TMath::Sqrt(mchTrack.getSigma2Phi());
  auto cutTanl = 3 * TMath::Sqrt(mchTrack.getSigma2Tanl());
  return (distance < cutDistance) and (dPhi < cutPhi) and (dTheta < cutTanl);
}

//_________________________________________________________________________________________________
bool MUONMatcher::matchCutDisabled(const GlobalMuonTrack &mchTrack,
                                   const MFTTrack &mftTrack) {
  return true;
}

//_________________________________________________________________________________________________
void MUONMatcher::setCutFunction(
    bool (MUONMatcher::*func)(const GlobalMuonTrack &, const MFTTrack &)) {
  mCutFunc = func;
  auto npars = 0;
  // Setting default parameters
  if (func == &MUONMatcher::matchCutDistance)
    npars = 1;
  if (func == &MUONMatcher::matchCutDistanceSigma)
    npars = 1;
  if (func == &MUONMatcher::matchCutDistanceAndAngles)
    npars = 3;
  for (auto par = mCutParams.size(); par < npars; par++)
    setCutParam(par, 1.0);
}

//_________________________________________________________________________________________________
void MUONMatcher::finalize() { // compute labels and populates mMatchingHelper
  auto GTrackID = 0;
  auto nFakes = 0;
  auto nNoMatch = 0;
  auto nCloseMatches = 0;

  std::cout << "Computing Track Labels..." << std::endl;

  for (auto &gTrack : mGlobalMuonTracks) {
    if (gTrack.closeMatch())
      nCloseMatches++;
    auto bestMFTTrackMatchID = gTrack.getBestMFTTrackMatchID();
    if (mVerbose) {
      std::cout << " GlobalTrack # " << GTrackID
                << " chi^2 = " << gTrack.getMatchingChi2() << std::endl;
      std::cout << "    bestMFTTrackMatchID :  " << bestMFTTrackMatchID
                << std::endl;
    }
    auto MCHlabel = mchTrackLabels.getLabels(GTrackID);
    o2::MCCompLabel thisLabel{MCHlabel[0].getTrackID(),
                              MCHlabel[0].getEventID(), -1, true};
    if (bestMFTTrackMatchID >= 0) {
      auto MFTlabel = mftTrackLabels.getLabels(bestMFTTrackMatchID);
      if (mVerbose) {
        std::cout << "    MFT Label:  ";
        MFTlabel[0].print();
        std::cout << "    MCH Label:  ";
        MCHlabel[0].print();
      }

      if ((MCHlabel[0].getTrackID() == MFTlabel[0].getTrackID()) and
          (MCHlabel[0].getEventID() == MFTlabel[0].getEventID())) {
        thisLabel = MCHlabel[0];
        thisLabel.setFakeFlag(false);
        gTrack.computeResiduals2Cov(mMFTTracks[bestMFTTrackMatchID]);

      } else {
        nFakes++;
      }
    } else {
      nNoMatch++;
    }
    if (mVerbose) {
      std::cout << "    Global Track Label => ";
      thisLabel.print();
      std::cout << std::endl;
    }
    mGlobalTrackLabels.addElement(mGlobalTrackLabels.getIndexedSize(),
                                  thisLabel);
    GTrackID++;
  }

  auto nCorrectMatches = GTrackID - nFakes - nNoMatch;
  auto nTracks = mGlobalMuonTracks.size();

  MatchingHelper &helper = mMatchingHelper;
  helper.nMCHTracks = nTracks;
  helper.nCloseMatches = nCloseMatches;
  helper.nCorrectMatches = nCorrectMatches;
  helper.nFakes = nFakes;
  helper.nNoMatch = nNoMatch;

  std::cout << "********************************** Matching Summary "
               "********************************** "
            << std::endl;
  std::cout << helper.nMCHTracks << " MCH Tracks." << std::endl;
  std::cout << helper.nNoMatch << " dangling MCH tracks ("
            << 100.0 * nNoMatch / nTracks << "%)" << std::endl;
  std::cout << helper.nGMTracks() << " global muon tracks (efficiency = "
            << 100.0 * helper.getPairingEfficiency() << "%)" << std::endl;
  std::cout << helper.nCorrectMatches
            << " Correct Match GM tracks (Correct Match Ratio = "
            << 100.0 * helper.getCorrectMatchRatio() << "%)" << std::endl;
  std::cout << nCloseMatches
            << " Close matches - correct MFT match in search window"
            << " (" << 100. * nCloseMatches / (helper.nGMTracks()) << "%)"
            << std::endl;
  std::cout << helper.nFakes << " Fake GM tracks (contamination = "
            << 100.0 * (1.0 - helper.getCorrectMatchRatio()) << ")"
            << std::endl;
  std::cout << "***************************************************************"
               "*********************** "
            << std::endl;
  std::cout << " Annotation: " << helper.Annotation() << std::endl;
  std::cout << "***************************************************************"
               "*********************** "
            << std::endl;
}

//_________________________________________________________________________________________________
void MUONMatcher::saveGlobalMuonTracks() {

  TFile outFile("GlobalMuonTracks.root", "RECREATE");
  TTree outTree("o2sim", "Global Muon Tracks");
  std::vector<GlobalMuonTrack> *tracks = &mGlobalMuonTracks;
  MCLabels *trackLabels = &mGlobalTrackLabels;
  outTree.Branch("GlobalMuonTrack", &tracks);
  outTree.Branch("GlobalMuonTrackMCTruth", &trackLabels);
  outTree.Fill();
  outTree.Write();
  outFile.WriteObjectAny(&mMatchingHelper, "MatchingHelper", "Matching Helper");
  outFile.Close();
  std::cout << "Global Muon Tracks saved to GlobalMuonTracks.root" << std::endl;

  std::ofstream matcherConfig("MatchingConfig.txt");
  matcherConfig << mMatchingHelper.MatchingConfig() << std::endl;
  matcherConfig.close();
}

//_________________________________________________________________________________________________
void MUONMatcher::fitTracks() {
  std::cout << "Fitting global muon tracks..." << std::endl;

  auto GTrackID = 0;
  for (auto &gTrack : mGlobalMuonTracks) {
    if (gTrack.getBestMFTTrackMatchID() >= 0) {
      if (mVerbose)
        std::cout << "Fitting Global Track # " << GTrackID
                  << " with MFT track # " << gTrack.getBestMFTTrackMatchID()
                  << ":" << std::endl;
      fitGlobalMuonTrack(gTrack);
    } else {
      if (mVerbose)
        std::cout << "No matching candidate for MCH Track " << GTrackID
                  << std::endl;
    }
    GTrackID++;
  }
  std::cout << "Finished fitting global muon tracks." << std::endl;
}

//_________________________________________________________________________________________________
void MUONMatcher::fitGlobalMuonTrack(GlobalMuonTrack &gTrack) {

  const auto &mftTrack = mMFTTracks[gTrack.getBestMFTTrackMatchID()];
  auto ncls = mftTrack.getNumberOfPoints();
  auto offset = mftTrack.getExternalClusterIndexOffset();
  auto invQPt0 = gTrack.getInvQPt();
  auto sigmainvQPtsq = gTrack.getCovariances()(4, 4);

  // initialize the starting track parameters and cluster
  auto nPoints = mftTrack.getNumberOfPoints();
  auto k = TMath::Abs(o2::constants::math::B2C * mField_z);
  auto Hz = std::copysign(1, mField_z);
  if (mVerbose) {

    std::cout << "\n ***************************** Start Fitting new track "
                 "***************************** \n";
    std::cout << "N Clusters = " << ncls << std::endl;
    std::cout << "  Best MFT Track Match ID " << gTrack.getBestMFTTrackMatchID()
              << std::endl;
    std::cout << "  MCHTrack: X = " << gTrack.getX() << " Y = " << gTrack.getY()
              << " Z = " << gTrack.getZ() << " Tgl = " << gTrack.getTanl()
              << "  Phi = " << gTrack.getPhi() << " pz = " << gTrack.getPz()
              << " qpt = " << 1.0 / gTrack.getInvQPt() << std::endl;
  }

  /// Compute the initial track parameters to seed the Kalman filter

  int first_cls, last_cls;
  // Vertexing
  first_cls = nPoints - 1;
  last_cls = 0;

  auto firstMFTclsEntry = mtrackExtClsIDs[offset + ncls - 1];
  auto &firstMFTcluster = mMFTClusters[firstMFTclsEntry];
  auto lastMFTclsEntry = mtrackExtClsIDs[offset];
  auto &lastMFTcluster = mMFTClusters[lastMFTclsEntry];

  const auto &x0 = firstMFTcluster.getX();
  const auto &y0 = firstMFTcluster.getY();
  const auto &z0 = firstMFTcluster.getZ();

  const auto &xf = lastMFTcluster.getX();
  const auto &yf = lastMFTcluster.getY();
  const auto &zf = lastMFTcluster.getZ();

  auto deltaX = x0 - xf;
  auto deltaY = y0 - yf;
  auto deltaZ = z0 - zf;
  auto deltaR = TMath::Sqrt(deltaX * deltaX + deltaY * deltaY);
  auto tanl0 =
      0.5 * TMath::Sqrt2() * (deltaZ / deltaR) *
      TMath::Sqrt(
          TMath::Sqrt((invQPt0 * deltaR * k) * (invQPt0 * deltaR * k) + 1) + 1);
  auto phi0 =
      TMath::ATan2(deltaY, deltaX) - 0.5 * Hz * invQPt0 * deltaZ * k / tanl0;
  auto sigmax0sq = firstMFTcluster.sigmaX2;
  auto sigmay0sq = firstMFTcluster.sigmaY2;
  auto sigmax1sq = lastMFTcluster.sigmaX2;
  auto sigmay1sq = lastMFTcluster.sigmaY2;
  auto sigmaDeltaXsq = sigmax0sq + sigmax1sq;
  auto sigmaDeltaYsq = sigmay0sq + sigmay1sq;

  gTrack.setX(x0);
  gTrack.setY(y0);
  gTrack.setZ(z0);
  gTrack.setPhi(phi0);
  gTrack.setTanl(tanl0);
  gTrack.setInvQPt(invQPt0);

  if (mVerbose) {

    std::cout << "  MFTTrack: X = " << mftTrack.getX()
              << " Y = " << mftTrack.getY() << " Z = " << mftTrack.getZ()
              << " Tgl = " << mftTrack.getTanl()
              << "  Phi = " << mftTrack.getPhi() << " pz = " << mftTrack.getPz()
              << " qpt = " << 1.0 / mftTrack.getInvQPt() << std::endl;
    std::cout << "  initTrack GlobalTrack: X = " << x0 << " Y = " << y0
              << " Z = " << z0 << " Tgl = " << tanl0 << "  Phi = " << phi0
              << " pz = " << gTrack.getPz()
              << " qpt = " << 1.0 / gTrack.getInvQPt() << std::endl;
  }

  auto deltaR2 = deltaR * deltaR;
  auto deltaR3 = deltaR2 * deltaR;
  auto deltaR4 = deltaR2 * deltaR2;
  auto k2 = k * k;
  auto A =
      TMath::Sqrt(gTrack.getInvQPt() * gTrack.getInvQPt() * deltaR2 * k2 + 1);
  auto A2 = A * A;
  auto B = A + 1.0;
  auto B3 = B * B * B;
  auto B12 = TMath::Sqrt(B);
  auto B32 = B * B12;
  auto C = invQPt0 * k;
  auto C2 = C * C;
  auto D = 1.0 / (A2 * B3 * B * deltaR4);
  auto E = D * deltaZ / (B * deltaR);
  auto J = 2 * B * deltaR3 * deltaR3 * k2;
  auto K = 0.5 * A * B - 0.25 * C2 * deltaR2;
  auto N = -0.5 * B3 * C * Hz * deltaR3 * deltaR4 * k2;
  auto O = 0.125 * C2 * deltaR4 * deltaR4 * k2;
  auto P = -K * k * Hz * deltaR / A;
  auto Q = deltaZ * deltaZ / (A2 * B * deltaR3 * deltaR3);
  auto R = 0.25 * C * deltaZ * TMath::Sqrt2() * deltaR * k / (A * B12);

  SMatrix55Sym lastParamCov;
  lastParamCov(0, 0) = sigmax0sq; // <X,X>
  lastParamCov(0, 1) = 0;         // <Y,X>
  lastParamCov(0, 2) = 0;         // <PHI,X>
  lastParamCov(0, 3) = 0;         // <TANL,X>
  // lastParamCov(0, 4) = 0;         // <INVQPT,X>

  lastParamCov(1, 1) = sigmay0sq; // <Y,Y>
  lastParamCov(1, 2) = 0;         // <PHI,Y>
  lastParamCov(1, 3) = 0;         // <TANL,Y>
  // lastParamCov(1, 4) = 0;         // <INVQPT,Y>

  lastParamCov(2, 2) = D * J * K * K * sigmainvQPtsq; // <PHI,PHI>
  lastParamCov(2, 3) = E * K * N * sigmainvQPtsq;     //  <TANL,PHI>
  // lastParamCov(2, 4) = P * sigmainvQPtsq * TMath::Sqrt2() / B32; //
  // <INVQPT,PHI>

  lastParamCov(3, 3) = Q * O * sigmainvQPtsq; // <TANL,TANL>
  // lastParamCov(3, 4) = R * sigmainvQPtsq;     // <INVQPT,TANL>

  // lastParamCov(4, 4) = sigmainvQPtsq; // <INVQPT,INVQPT>

  gTrack.setCovariances(lastParamCov);
  gTrack.setTrackChi2(0.);

  for (int icls = ncls - 1; icls > -1; --icls) {
    auto clsEntry = mtrackExtClsIDs[offset + icls];
    auto &thiscluster = mMFTClusters[clsEntry];
    computeCluster(gTrack, thiscluster);
  }
}

//_________________________________________________________________________________________________
bool MUONMatcher::computeCluster(GlobalMuonTrack &track, MFTCluster &cluster) {

  const auto &clx = cluster.getX();
  const auto &cly = cluster.getY();
  const auto &clz = cluster.getZ();

  // add MCS effects for the new cluster
  using o2::mft::constants::LayerZPosition;
  int startingLayerID, newLayerID;

  double dZ = clz - track.getZ();
  // LayerID of each cluster from ZPosition // TODO: Use ChipMapping
  for (auto layer = 10; layer--;)
    if (track.getZ() < LayerZPosition[layer] + .3 &
        track.getZ() > LayerZPosition[layer] - .3)
      startingLayerID = layer;
  for (auto layer = 10; layer--;)
    if (clz<LayerZPosition[layer] + .3 & clz> LayerZPosition[layer] - .3)
      newLayerID = layer;
  // Number of disks crossed by this tracklet
  int NDisksMS;
  if (clz - track.getZ() > 0)
    NDisksMS = (startingLayerID % 2 == 0)
                   ? (startingLayerID - newLayerID) / 2
                   : (startingLayerID - newLayerID + 1) / 2;
  else
    NDisksMS = (startingLayerID % 2 == 0)
                   ? (newLayerID - startingLayerID + 1) / 2
                   : (newLayerID - startingLayerID) / 2;

  double MFTDiskThicknessInX0 = 0.1 / 5.0; // FIXME!

  if ((NDisksMS * MFTDiskThicknessInX0) != 0)
    track.addMCSEffect(-1, NDisksMS * MFTDiskThicknessInX0);

  track.propagateToZhelix(clz, mField_z);

  const std::array<float, 2> &pos = {clx, cly};
  const std::array<float, 2> &cov = {cluster.sigmaX2, cluster.sigmaY2};

  if (track.update(pos, cov)) {
    if (mVerbose) {
      std::cout << "   New Cluster: X = " << clx << " Y = " << cly
                << " Z = " << clz << std::endl;
      std::cout << "   AfterKalman: X = " << track.getX()
                << " Y = " << track.getY() << " Z = " << track.getZ()
                << " Tgl = " << track.getTanl() << "  Phi = " << track.getPhi()
                << " pz = " << track.getPz()
                << " qpt = " << 1.0 / track.getInvQPt() << std::endl;
      std::cout << std::endl;
      // Outputs track covariance matrix:
      // param.getCovariances().Print();
    }
  }
  return true;
}

//_________________________________________________________________________________________________
GlobalMuonTrack MUONMatcher::MCHtoGlobal(MCHTrack &mchTrack) {
  // Convert a MCH Track parameters and covariances matrix to the
  // GlobalMuonTrack format. Must be called after propagation on the absorber

  using SMatrix55Std = ROOT::Math::SMatrix<double, 5>;

  GlobalMuonTrack convertedTrack;

  // Parameter conversion
  double alpha1, alpha3, alpha4, x2, x3, x4;

  alpha1 = mchTrack.getNonBendingSlope();
  alpha3 = mchTrack.getBendingSlope();
  alpha4 = mchTrack.getInverseBendingMomentum();

  x2 = TMath::ATan2(-alpha3, -alpha1);
  x3 = -1. / TMath::Sqrt(alpha3 * alpha3 + alpha1 * alpha1);
  x4 = alpha4 * -x3 * TMath::Sqrt(1 + alpha3 * alpha3);

  auto K = alpha1 * alpha1 + alpha3 * alpha3;
  auto K32 = K * TMath::Sqrt(K);
  auto L = TMath::Sqrt(alpha3 * alpha3 + 1);

  // Covariances matrix conversion
  SMatrix55Std jacobian;
  SMatrix55Sym covariances;

  if (mVerbose) {

    std::cout << " MCHtoGlobal - MCH Covariances:\n";
    std::cout << " mchTrack.getCovariances()(0, 0) =  "
              << mchTrack.getCovariances()(0, 0)
              << " ; mchTrack.getCovariances()(2, 2) = "
              << mchTrack.getCovariances()(2, 2) << std::endl;
  }
  covariances(0, 0) = mchTrack.getCovariances()(0, 0);
  covariances(0, 1) = mchTrack.getCovariances()(0, 1);
  covariances(0, 2) = mchTrack.getCovariances()(0, 2);
  covariances(0, 3) = mchTrack.getCovariances()(0, 3);
  covariances(0, 4) = mchTrack.getCovariances()(0, 4);

  covariances(1, 1) = mchTrack.getCovariances()(1, 1);
  covariances(1, 2) = mchTrack.getCovariances()(1, 2);
  covariances(1, 3) = mchTrack.getCovariances()(1, 3);
  covariances(1, 4) = mchTrack.getCovariances()(1, 4);

  covariances(2, 2) = mchTrack.getCovariances()(2, 2);
  covariances(2, 3) = mchTrack.getCovariances()(2, 3);
  covariances(2, 4) = mchTrack.getCovariances()(2, 4);

  covariances(3, 3) = mchTrack.getCovariances()(3, 3);
  covariances(3, 4) = mchTrack.getCovariances()(3, 4);

  covariances(4, 4) = mchTrack.getCovariances()(4, 4);

  jacobian(0, 0) = 1;

  jacobian(1, 2) = 1;

  jacobian(2, 1) = -alpha3 / K;
  jacobian(2, 3) = alpha1 / K;

  jacobian(3, 1) = alpha1 / K32;
  jacobian(3, 3) = alpha3 / K32;

  jacobian(4, 1) = -alpha1 * alpha4 * L / K32;
  jacobian(4, 3) = alpha3 * alpha4 * (1 / (TMath::Sqrt(K) * L) - L / K32);
  jacobian(4, 4) = L / TMath::Sqrt(K);

  // jacobian*covariances*jacobian^T
  covariances = ROOT::Math::Similarity(jacobian, covariances);

  // Set output
  convertedTrack.setX(mchTrack.getNonBendingCoor());
  convertedTrack.setY(mchTrack.getBendingCoor());
  convertedTrack.setZ(mchTrack.getZ());
  convertedTrack.setPhi(x2);
  convertedTrack.setTanl(x3);
  convertedTrack.setInvQPt(x4);
  convertedTrack.setCharge(mchTrack.getCharge());
  convertedTrack.setCovariances(covariances);

  return convertedTrack;
}

//_________________________________________________________________________________________________
double MUONMatcher::matchMFT_MCH_TracksXY(const GlobalMuonTrack &mchTrack,
                                          const MFTTrack &mftTrack) {
  // Calculate Matching Chi2 - X and Y positions

  SMatrix55Sym I = ROOT::Math::SMatrixIdentity();
  SMatrix25 H_k;
  SMatrix22 V_k;
  SVector2 m_k(mftTrack.getX(), mftTrack.getY()), r_k_kminus1;
  SMatrix5 GlobalMuonTrackParameters = mchTrack.getParameters();
  SMatrix55Sym GlobalMuonTrackCovariances = mchTrack.getCovariances();
  V_k(0, 0) = mftTrack.getCovariances()(0, 0);
  V_k(1, 1) = mftTrack.getCovariances()(1, 1);
  H_k(0, 0) = 1.0;
  H_k(1, 1) = 1.0;

  // Covariance of residuals
  SMatrix22 invResCov =
      (V_k + ROOT::Math::Similarity(H_k, GlobalMuonTrackCovariances));
  invResCov.Invert();

  // Kalman Gain Matrix
  SMatrix52 K_k =
      GlobalMuonTrackCovariances * ROOT::Math::Transpose(H_k) * invResCov;

  // Update Parameters
  r_k_kminus1 =
      m_k - H_k * GlobalMuonTrackParameters; // Residuals of prediction
  // GlobalMuonTrackParameters = GlobalMuonTrackParameters + K_k * r_k_kminus1;

  // Update covariances Matrix
  // SMatrix55Std updatedCov = (I - K_k * H_k) * GlobalMuonTrackCovariances;

  auto matchChi2Track = ROOT::Math::Similarity(r_k_kminus1, invResCov);

  // GlobalMuonTrack matchTrack(mchTrack);
  // matchTrack.setZ(mchTrack.getZ());
  // matchTrack.setParameters(GlobalMuonTrackParameters);
  // matchTrack.setCovariances(GlobalMuonTrackCovariances);
  // matchTrack.setMatchingChi2(matchChi2Track);
  return matchChi2Track;
}

//_________________________________________________________________________________________________
double
MUONMatcher::matchMFT_MCH_TracksXYPhiTanl(const GlobalMuonTrack &mchTrack,
                                          const MFTTrack &mftTrack) {
  // Match two tracks evaluating positions & angles

  SMatrix55Sym I = ROOT::Math::SMatrixIdentity();
  SMatrix45 H_k;
  SMatrix44 V_k;
  SVector4 m_k(mftTrack.getX(), mftTrack.getY(), mftTrack.getPhi(),
               mftTrack.getTanl()),
      r_k_kminus1;
  SMatrix5 GlobalMuonTrackParameters = mchTrack.getParameters();
  SMatrix55Sym GlobalMuonTrackCovariances = mchTrack.getCovariances();
  V_k(0, 0) = mftTrack.getCovariances()(0, 0);
  V_k(1, 1) = mftTrack.getCovariances()(1, 1);
  V_k(2, 2) = mftTrack.getCovariances()(2, 2);
  V_k(3, 3) = mftTrack.getCovariances()(3, 3);
  H_k(0, 0) = 1.0;
  H_k(1, 1) = 1.0;
  H_k(2, 2) = 1.0;
  H_k(3, 3) = 1.0;

  // Covariance of residuals
  SMatrix44 invResCov =
      (V_k + ROOT::Math::Similarity(H_k, GlobalMuonTrackCovariances));
  invResCov.Invert();

  // Kalman Gain Matrix
  SMatrix54 K_k =
      GlobalMuonTrackCovariances * ROOT::Math::Transpose(H_k) * invResCov;

  // Update Parameters
  r_k_kminus1 =
      m_k - H_k * GlobalMuonTrackParameters; // Residuals of prediction
  // GlobalMuonTrackParameters = GlobalMuonTrackParameters + K_k * r_k_kminus1;

  // Update covariances Matrix
  // SMatrix55Std updatedCov = (I - K_k * H_k) * GlobalMuonTrackCovariances;

  auto matchChi2Track = ROOT::Math::Similarity(r_k_kminus1, invResCov);

  // GlobalMuonTrack matchTrack(mchTrack);
  // matchTrack.setZ(mchTrack.getZ());
  // matchTrack.setParameters(GlobalMuonTrackParameters);
  // matchTrack.setCovariances(GlobalMuonTrackCovariances);
  // matchTrack.setMatchingChi2(matchChi2Track);
  return matchChi2Track;
}

//_________________________________________________________________________________________________
double MUONMatcher::matchMFT_MCH_TracksAllParam(const GlobalMuonTrack &mchTrack,
                                                const MFTTrack &mftTrack) {
  // Match two tracks evaluating all parameters: X,Y, phi, tanl & q/pt

  SMatrix55Sym I = ROOT::Math::SMatrixIdentity(), H_k, V_k;
  SVector5 m_k(mftTrack.getX(), mftTrack.getY(), mftTrack.getPhi(),
               mftTrack.getTanl(), mftTrack.getInvQPt()),
      r_k_kminus1;
  SMatrix5 GlobalMuonTrackParameters = mchTrack.getParameters();
  SMatrix55Sym GlobalMuonTrackCovariances = mchTrack.getCovariances();
  V_k(0, 0) = mftTrack.getCovariances()(0, 0);
  V_k(1, 1) = mftTrack.getCovariances()(1, 1);
  V_k(2, 2) = mftTrack.getCovariances()(2, 2);
  V_k(3, 3) = mftTrack.getCovariances()(3, 3);
  V_k(4, 4) = mftTrack.getCovariances()(4, 4);
  H_k(0, 0) = 1.0;
  H_k(1, 1) = 1.0;
  H_k(2, 2) = 1.0;
  H_k(3, 3) = 1.0;
  H_k(4, 4) = 1.0;

  // Covariance of residuals
  SMatrix55Std invResCov =
      (V_k + ROOT::Math::Similarity(H_k, GlobalMuonTrackCovariances));
  invResCov.Invert();

  // Kalman Gain Matrix
  SMatrix55Std K_k =
      GlobalMuonTrackCovariances * ROOT::Math::Transpose(H_k) * invResCov;

  // Update Parameters
  r_k_kminus1 =
      m_k - H_k * GlobalMuonTrackParameters; // Residuals of prediction
  // GlobalMuonTrackParameters = GlobalMuonTrackParameters + K_k * r_k_kminus1;

  // Update covariances Matrix
  // SMatrix55Std updatedCov = (I - K_k * H_k) * GlobalMuonTrackCovariances;

  auto matchChi2Track = ROOT::Math::Similarity(r_k_kminus1, invResCov);

  // GlobalMuonTrack matchTrack(mchTrack);
  // matchTrack.setZ(mchTrack.getZ());
  // matchTrack.setParameters(GlobalMuonTrackParameters);
  // matchTrack.setCovariances(GlobalMuonTrackCovariances);
  // matchTrack.setMatchingChi2(matchChi2Track);
  return matchChi2Track;
}

Float_t EtaToTheta(Float_t arg) {
  return (180. / TMath::Pi()) * 2. * atan(exp(-arg));
}

//_________________________________________________________________________________________________ 
double MUONMatcher::Hiroshima(const GlobalMuonTrack &mchTrack,
			      const MFTTrack &mftTrack) {

  //Hiroshima's Matching function

  //Matching constants
  Double_t LAbs = 415.;  //Absorber Length[cm]
  Double_t mumass = 0.106; //mass of muon [GeV/c^2]
  Double_t l;  //the length that extrapolated MCHtrack passes through absorber

  if (mMatchingPlaneZ >= -90.0){
    l = LAbs;
  }
  else{
    l = 505.0 + mMatchingPlaneZ;
  }

  //defference between MFTtrack and MCHtrack

  auto dx = mftTrack.getX() - mchTrack.getX();
  auto dy = mftTrack.getY() - mchTrack.getY();
  auto dthetax = TMath::ATan(mftTrack.getPx() / TMath::Abs(mftTrack.getPz())) - TMath::ATan(mchTrack.getPx() / TMath::Abs(mchTrack.getPz()));
  auto dthetay = TMath::ATan(mftTrack.getPy() / TMath::Abs(mftTrack.getPz())) - TMath::ATan(mchTrack.getPy() / TMath::Abs(mchTrack.getPz()));

  //Multiple Scattering(=MS)

  auto pMCH = mchTrack.getP();
  auto lorentzbeta = pMCH/TMath::Sqrt(mumass*mumass+pMCH*pMCH);
  auto zMS = copysign(1.0,mchTrack.getCharge());
  auto thetaMS = 13.6/(1000.0*pMCH*lorentzbeta*1.0)*zMS*TMath::Sqrt(60.0*l/LAbs)*(1.0+0.038*TMath::Log(60.0*l/LAbs));
  auto xMS = thetaMS*l/TMath::Sqrt(3.0);

  //normalize by theoritical Multiple Coulomb Scattering width to be momentum-independent
  //make the dx and dtheta dimensionless

  auto dxnorm = dx/xMS;
  auto dynorm = dy/xMS;
  auto dthetaxnorm = dthetax/thetaMS;
  auto dthetaynorm = dthetay/thetaMS;

  //rotate distribution

  auto dxrot = dxnorm*TMath::Cos(TMath::Pi()/4.0)-dthetaxnorm*TMath::Sin(TMath::Pi()/4.0);
  auto dthetaxrot = dxnorm*TMath::Sin(TMath::Pi()/4.0)+dthetaxnorm*TMath::Cos(TMath::Pi()/4.0);
  auto dyrot = dynorm*TMath::Cos(TMath::Pi()/4.0)-dthetaynorm*TMath::Sin(TMath::Pi()/4.0);
  auto dthetayrot = dynorm*TMath::Sin(TMath::Pi()/4.0)+dthetaynorm*TMath::Cos(TMath::Pi()/4.0);

  //convert ellipse to circle
  
  auto k = 0.7;  //need to optimize!!
  auto dxcircle = dxrot;
  auto dycircle = dyrot;
  auto dthetaxcircle = dthetaxrot/k;
  auto dthetaycircle = dthetayrot/k;

  //score

  auto scoreX = TMath::Sqrt( dxcircle*dxcircle + dthetaxcircle*dthetaxcircle );
  auto scoreY = TMath::Sqrt( dycircle*dycircle + dthetaycircle*dthetaycircle );
  auto score = TMath::Sqrt( scoreX*scoreX + scoreY*scoreY );

  return score;
};
