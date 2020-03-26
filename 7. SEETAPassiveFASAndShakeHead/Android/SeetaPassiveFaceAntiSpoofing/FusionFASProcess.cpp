#include "FusionFASProcess.h"
#include <iostream>
#include "seeta/Struct.h"
#include "seeta/ImageProcess.h"
using namespace std;


#ifdef ANDROID_CRYPTO

#include "android/log.h"
#include <string>
#include "simple_crypto.h"
#define LOG_TAG "FaceDetectorJni"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG,  __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

void soft_lock() {
    std::string c2s_filename = "sdcard/seetatech_receipts";
    std::string req_orig = "";
    std::string ver_orig = "";
    // called in client
    request_receipts(c2s_filename.c_str(), req_orig);

    // called in client
    verify_receipts(get_output_filename(c2s_filename).c_str(), ver_orig);

    if(req_orig != ver_orig) {
        LOGE("FaceDetector512jni Authorization Faild!");
        exit(EXIT_SUCCESS);
    }
}

bool checkTimeLimit()
{
    int max_year = TIME_LOCK_YEAR;
    int max_month = TIME_LOCK_MONTH;
    int max_day = TIME_LOCK_DAY;

    LOGE("1. st");
    time_t now;
    time(&now);
    struct tm* timenow = localtime(&now);
    LOGE("2. st");

    if(timenow->tm_year + 1900 > max_year)
    {
        LOGE("Exceed to Time Limit!");
        return false;
    }

    if((timenow->tm_year + 1900) == max_year)
    {
       if(timenow->tm_mon >= max_month)
        {
            LOGE("Exceed to Time Limit!");
            return false;
        }

        if(timenow->tm_mon == max_month - 1)
        {
             if(timenow->tm_mday > max_day)
              {
                  LOGE("Exceed to Time Limit!");
                  return false;
              }
        }

    }

    LOGE("3. st");
    return true;
}
#endif

#include <cmath>
FusionFASProcess::FusionFASProcess(const std::string ModelPath,
	const int shakeHeadAngleThreshold,
	const int nodHeadAngleThreshold,
	const double clarityTheshold,
	const double fuseThreshold,
	SystemState &systemState,
	const int firstPhaseFrameNum,
	const int detectFrameNum)
{
#ifdef ANDROID_CRYPTO

    soft_lock();
    if(checkTimeLimit() == false)
    {
       exit(EXIT_SUCCESS);
    }
	
#endif

	antiSpoofing = std::make_shared<SEETAPassiveFaceAntiSpoofing>(ModelPath);
	ClarityEstimator = std::make_shared<ClarityEstimateClass>();
	
	auto poseEstimation = std::make_shared<VIPLPoseEstimation>((ModelPath + "VIPLPoseEstimation1.1.0.ext.dat").c_str());
	auto closeEyeDetector = std::make_shared<VIPLEyeBlinkDetector>((ModelPath + "VIPLEyeBlinkDetector1.3.99x99.raw.dat").c_str());
	
	auto ShakeHead = std::make_shared<ClassShakeHead>(poseEstimation, shakeHeadAngleThreshold);
	auto BlinkEye = std::make_shared<EyeAction>(ModelPath + "VIPLEyeBlinkDetector1.3.99x99.raw.dat");
	auto NodHead = std::make_shared<ClassNodHead>(poseEstimation, nodHeadAngleThreshold);

	self::action_detetors.insert(std::make_pair(pleaseTrun, ShakeHead));
	self::action_detetors.insert(std::make_pair(pleaseBlink, BlinkEye));
	self::action_detetors.insert(std::make_pair(pleaseNodHead, NodHead));
	
	self::set_actions({ pleaseTrun });

	thisFuseThreshold = fuseThreshold;
	thisClarityTheshold = clarityTheshold;
	thisDetectFrameNum = detectFrameNum;
	thisFirstPhaseFrameNum = firstPhaseFrameNum;
	reset(systemState);
	
	this->poseEstimation = poseEstimation;	// for snapshot getter
	this->closeEyeDetector = closeEyeDetector;
}


FusionFASProcess::~FusionFASProcess()
{

}

VIPLImageData FusionFASProcess::getSnapshot()
{
	return snapshot;
}


void FusionFASProcess::resetSnapshot()
{
	snapshot.width = 0;
	snapshot.height = 0;
	snapshot.channels = 0;
	snapshot.data = nullptr;
	snapshot_data.reset();
	snapshot_length = 0;
	snapshot_score = 0;
}

/**
 * \brief 加权重的姿态评估
 * \param srcImg 原图
 * \param faceInfo 人脸框
 * \param yawAngle 
 * \param pitchAngle 
 * \param rollAngle 
 * \return 加权重的人脸姿态得分
 */
float FusionFASProcess::evaluateSnapshotPoseScore(const VIPLImageData& srcImg, const VIPLFaceInfo& faceInfo, const float &yawAngle, const float &pitchAngle, const float & rollAngle) const
{
	const int MIN_FACE_WIDTH = 64;
	const int SATISFIED_WIDTH = 128;

	float face_with = std::sqrt(faceInfo.width * faceInfo.height);
	float size_score = face_with >= SATISFIED_WIDTH
		? 1
		: (face_with - MIN_FACE_WIDTH) / (SATISFIED_WIDTH - MIN_FACE_WIDTH);
	if (size_score < 0) size_score = 0;

	float yaw_score = (90 - fabs(yawAngle) - 30) / 60;
	float pitch_score = (90 - fabs(pitchAngle) - 45) / 45;
	float roll_score = (90 - fabs(rollAngle) - 45) / 45;
	if (yaw_score < 0) yaw_score = 0;
	if (pitch_score < 0) pitch_score = 0;
	float pose_score = 0.45 * yaw_score + 0.45 * pitch_score + 0.1 * roll_score;

	float final_score = 0.25 * size_score + 0.75 * pose_score;

	return pose_score;	//返回是  是否是最好姿态
}

void FusionFASProcess::updateSnapshot(const VIPLImageData& image, float score)
{
	if (snapshot_score > score) return;
	snapshot_score = score;
	size_t need_length = image.channels * image.height * image.width;
	if (need_length > snapshot_length)
	{
		snapshot_data.reset(new uint8_t[need_length], std::default_delete<uint8_t[]>());
		snapshot_length = need_length;
	}
	std::memcpy(snapshot_data.get(), image.data, need_length);
	snapshot = image;
	snapshot.data = snapshot_data.get();
}

static float GetSnapshot(const seeta::Image& frame, seeta::Image &dest, const seeta::Rect& rect, float scalar)
{
	int pad_width = rect.width * scalar;
	int pad_height = rect.height * scalar;
	int padded_x = rect.x - pad_width;
	int padded_y = rect.y - pad_height;
	int padded_width = rect.width + 2 * pad_width;
	int padded_height = rect.height + 2 * pad_height;

	int frame_width = frame.width();
	int frame_height = frame.height();

	if (padded_x < 0)
	{
        padded_width += padded_x;
		padded_x = 0;
	}
	if (padded_y < 0)
	{
        padded_height += padded_y;
		padded_y = 0;
	}
	if (padded_x + padded_width > frame_width)
	{
		int edge = padded_x + padded_width - frame_width;
		padded_width -= edge;
	}
	if (padded_y + padded_height > frame_height)
	{
		int edge = padded_y + padded_height - frame_height;
		padded_height -= edge;
	}

    auto score = float(padded_width * padded_height) / (rect.width * scalar * rect.height * scalar);

    // force to 4:3
    int ratio_width = 3;
    int ratio_height = 4;
    int want_width = std::min(frame_width, rect.width + 2 * pad_width);
    int want_height = std::min(frame_height, rect.height + 2 * pad_height);

    int want_x = rect.x - (want_width - rect.width) / 2;
    int want_y = rect.y - (want_height - rect.height) / 2;

    int unit = std::min(want_width / ratio_width, want_height / ratio_height);
    int fixed_width = unit * ratio_width;
    int fixed_height = unit * ratio_height;
    int fixed_x = want_x - (fixed_width - want_width) / 2;
    int fixed_y = want_y - (fixed_height - want_height) / 2;
    // shift rect to remove pad
    fixed_x = std::max(fixed_x, 0);
    fixed_y = std::max(fixed_y, 0);
    fixed_x = std::min(fixed_x + fixed_width, frame_width) - fixed_width;
    fixed_y = std::min(fixed_y + fixed_height, frame_height) - fixed_height;

    if (fixed_width <= 0 || fixed_height <= 0)
	{
		dest = seeta::Image(0, 0, 0);
	}
	else
	{
        dest = seeta::crop(frame, seeta::Rect(fixed_x, fixed_y, fixed_width, fixed_height));
	}

	return score > 0.9 ? score : score / 2;	//分值代表是否在中心
}

/**
 * \brief 全图灰度值的均值
 * \param img 待测试图片
 * \return 全图灰度值的均值
 */
double FusionFASProcess::seeta_mean(const seeta::Image &img)
{
	auto gray = seeta::gray(img);
	auto count = gray.width() * gray.height();
	long sum = 0;
	for (int i = 0; i < count; ++i)
	{
		sum += gray.data()[i];
	}
	return double(sum) / count;
}
/**
 * \brief 光照情况判断	
 * \param img 待测试原图
 * \return 光照情况
 */
double FusionFASProcess::lightJudgment(const seeta::Image &img)
{
	double facialLightValue =seeta_mean(img);
	//移动端移植时，根据情况调节该值。
	const int lightThreshold = 130;
	//分值越高代表光照越强，
	return facialLightValue > lightThreshold ? 1 : static_cast<double>((facialLightValue / 130) / 2);

}
/**
 * \brief 清晰度判断，加了权重
 * \param clarity 清晰度
 * \return 加权重后的清晰度
 */
double FusionFASProcess::clarityJudgment(const double &clarity) const
{
	//清晰度判断
	return clarity > thisClarityTheshold ? clarity : clarity / 10;
}

void FusionFASProcess::updateSnapshot(const VIPLImageData& image, const VIPLFaceInfo& info, const  float &yawAngle, const  float &pitchAngle, const  float & rollAngle,const double &clarity)
{
	double facialQualityValue = evaluateSnapshotPoseScore(image, info,yawAngle, pitchAngle, rollAngle);

	seeta::Image face;
	facialQualityValue*=GetSnapshot(seeta::Image(image.data, image.width, image.height, image.channels), face, seeta::Rect(info.x, info.y, info.width, info.height), 0.5);
	VIPLImageData vface(face.width(), face.height(), face.channels());
	vface.data = face.data();

	facialQualityValue *= lightJudgment(face);
	facialQualityValue *= clarityJudgment(clarity);
	//cout << "facial quality: " << facialQualityValue << endl;
	updateSnapshot(vface, facialQualityValue);
}

void FusionFASProcess::updateSnapshot_v2(const VIPLImageData& image, const VIPLFaceInfo& info, const  float &yawAngle, const  float &pitchAngle, const  float & rollAngle,const double &clarity)
{//snapshot更新并返回原图
	double facialQualityValue = evaluateSnapshotPoseScore(image, info,yawAngle, pitchAngle, rollAngle);

	seeta::Image face;
	facialQualityValue*=GetSnapshot(seeta::Image(image.data, image.width, image.height, image.channels), face, seeta::Rect(info.x, info.y, info.width, info.height), 0.5);
	// VIPLImageData vface(face.width(), face.height(), face.channels());
	// vface.data = face.data();

	facialQualityValue *= lightJudgment(face);
	facialQualityValue *= clarityJudgment(clarity);
	//cout << "facial quality: " << facialQualityValue << endl;
	updateSnapshot(image, facialQualityValue);
}

/**
* \brief 浜鸿劯娲讳綋妫€娴嬫椂锛屽垽鏂ǔ瀹氬嚱鏁帮紝鍦ㄤ汉鑴哥ǔ瀹氭椂锛屾娴嬫晥鏋滄洿濂?
* \param points 褰撳墠甯у叧閿偣
* \param prepoints 鍓嶄竴甯у叧閿偣
* \return 1绋冲畾銆?涓嶇ǔ瀹?
*/
int FusionFASProcess::isStable(const VIPLPoint *points, VIPLPoint *prepoints, int face_width)
{
	int noseStableMax = std::max<int>(4, std::ceil(face_width / 4)); //稳定判断条件中，可忍受的鼻子最大位移
	int noseDisplacement = sqrt(pow((points[2].x - prepoints[2].x), 2) + pow((points[2].y - prepoints[2].y), 2));
	if (noseDisplacement < noseStableMax)
	{
		return 1;	//可忍受的稳定程度
	}
	else
	{
		return 0;	//不可忍受的稳定程度
	}
}
/**
 * \brief 判断人脸是否为正面
 * \param yawAngle 头部左右转角度
 * \param pitchAngle 头部上下转角度
 * \param rollAngle 平面内转头角度
 * \return 是否为正面
 */
bool FusionFASProcess::isMiddle(const float &yawAngle, const float &pitchAngle, const float & rollAngle)
{
	//yaw 左右转，摄像头翻转后，左转为正，右转为负，合理范围正负20
	//pitch 上下转，仰头为正，低头为负。仰头的数值比低头小，合理范围-25到10
	//roll   平面内转头，也就是鼻子不动的情况下，左歪头与右歪头。摄像头翻转后，左歪头为负，合理范围正负25
	const int yawMax = 20, yawMin = -yawMax;
	const int pitchMax = 10, pitchMin = -25;
	const int rollMax = 25, rollMin = -rollMax;

	if (yawAngle<yawMax&&yawAngle>yawMin)
	{
		if (pitchAngle<pitchMax&&pitchAngle>pitchMin)
		{
			if (rollAngle<rollMax&&rollAngle>rollMin)
			{
				return true;
			}
		}
	}
	return false;
}
void FusionFASProcess::detecion(VIPLImageData VIPLImage, const VIPLFaceInfo &info, const VIPLPoint* points5, SystemState &state)
{
	if (state == passed || state == notPass) return;

	// qualityValue = qualityAssessor->Evaluate(VIPLImage, info);
	int stable = isStable(points5, prepoints,info.width);
	for (int i = 0; i < 5; i++){
		prepoints[i] = points5[i];
	}
	if (frameNum<thisFirstPhaseFrameNum)
	{
		poseEstimation->Estimate(VIPLImage, info, yawAngle, pitchAngle, rollAngle);
		if (isMiddle(yawAngle, pitchAngle, rollAngle))
		{
			if (stable == 1)
			{
				bool isCloseEye = closeEyeDetector->ClosedEyes(VIPLImage, points5);
				if (!isCloseEye)
				{
				clarity = ClarityEstimator->clarityEstimate(VIPLImage, info);
				passiveFASResult = antiSpoofing->matToTheSeetaNet(VIPLImage, info, points5);
				firstPhaseVector_clarity.push_back(clarity);
				firstPhaseVector_passiveFASResult.push_back(passiveFASResult);
				frameNum++;
				updateSnapshot_v2(VIPLImage, info, yawAngle, pitchAngle, rollAngle,clarity);//更新并返回原图，不进行裁剪
				}
			}
			state = detecting;
		}
		else
		{
			state = pleaseFaceTheCamera;
		}
	}
	else
	{
		for (int i = 0; i < action.size(); i++)
		{
			auto detected = action[i]->detect(VIPLImage, info, points5);
			if (detected)
			{
				action[i]->reset();
				state = passed;
				break;
			}
		}
		frameNum++;
	}

	if (frameNum > thisDetectFrameNum)
	{
		state = notPass;
	}
	if (frameNum == thisFirstPhaseFrameNum)
	{
		if (firstPhaseIsPass())
		{
			if (thisAction_list.empty()) {
				state = passed;
			}
			else {
				for (int i = 0; i < thisAction_list.size(); i++)
				{
					state = thisAction_list[i];
					action.push_back(action_detetors[state]);
				}
			}
		}
		else
		{
			state = notPass;
		}
	}
}
bool FusionFASProcess::firstPhaseIsPass()
{
	
	for (int i = 0; i <firstPhaseVector_clarity.size(); i++)
	{
		firstPhaseTotal_clarity += firstPhaseVector_clarity[i];
	}
	if ((firstPhaseTotal_clarity / thisFirstPhaseFrameNum) < thisClarityTheshold)
	{
		return false;
	}
	for (int i = 0; i < firstPhaseVector_passiveFASResult.size(); i++)
	{
		firstPhaseTotal_passive += firstPhaseVector_passiveFASResult[i];
	}
	if ((firstPhaseTotal_passive / thisFirstPhaseFrameNum) < thisFuseThreshold)
	{
		return false;
	}
	return true;
}
void FusionFASProcess::reset(SystemState &state)
{
	state = noFace;
	passiveFASIsPass=false;
	frameNum = 0;
	
	//reset the elements in the vector to zero
	for (vector<int>::size_type ix = 0; ix != firstPhaseVector_clarity.size(); ++ix)
	{
		firstPhaseVector_clarity[ix] = 0;
	}
	for (vector<int>::size_type ix = 0; ix != firstPhaseVector_passiveFASResult.size(); ++ix)
	{
		firstPhaseVector_passiveFASResult[ix] = 0;
	}

	firstPhaseTotal_clarity = 0.00;
	firstPhaseTotal_passive = 0.00;
	yawAngle = -999, pitchAngle = -999, rollAngle = -999;
	clarity = -999;
	passiveFASResult = -999;
	self::thisAction_list = self::thisAction_list_template;

	action.clear();
	
	resetSnapshot();
}
void FusionFASProcess::getLog(double &value1, double &value2, double &yaw,double &pitch)
{
	value1 = clarity;
	value2 = passiveFASResult;
	pitch = pitchAngle;
	yaw = yawAngle;
}

bool FusionFASProcess::set_actions(const std::vector<SystemState>& actions)
{
	std::vector<SystemState> new_Action_list_template;
	for (auto &action : actions) {
		if (action != pleaseTrun && action != pleaseBlink&&action != pleaseNodHead) return false;
		new_Action_list_template.push_back(action);
	}
	self::thisAction_list_template = std::move(new_Action_list_template);

	SystemState state;
	reset(state);
	return true;
}
