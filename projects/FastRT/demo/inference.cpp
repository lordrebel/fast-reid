#include <iostream>
#include <opencv2/opencv.hpp>

#include "fastrt/utils.h"
#include "fastrt/baseline.h"
#include "fastrt/sbs_resnet.h"
#include "fastrt/embedding_head.h"
using namespace fastrt;
using namespace nvinfer1;

/* Ex1. sbs_R50-ibn */
static const std::string WEIGHTS_PATH = "../sbs_R50-ibn.wts"; 
static const std::string ENGINE_PATH = "./sbs_R50-ibn.engine";

static const int MAX_BATCH_SIZE = 4;
static const int INPUT_H = 256;
static const int INPUT_W = 128;
static const int OUTPUT_SIZE = 2048;
static const int DEVICE_ID = 0;

static const FastreidPoolingType HEAD_POOLING = FastreidPoolingType::gempoolP;
static const FastreidBackboneType BACKBONE = FastreidBackboneType::r50; 
static const int LAST_STRIDE = 1;
static const bool WITH_IBNA = true; 
static const bool WITH_NL = true;
static const int EMBEDDING_DIM = 0; 


int main(int argc, char** argv) {

    trt::ModelConfig modelCfg { 
        WEIGHTS_PATH,
        MAX_BATCH_SIZE,
        INPUT_H,
        INPUT_W,
        OUTPUT_SIZE,
        DEVICE_ID};

    FastreidConfig reidCfg { 
        HEAD_POOLING,
        BACKBONE,
        LAST_STRIDE,
        WITH_IBNA,
        WITH_NL,
        EMBEDDING_DIM};

    std::cout << "[ModelConfig]: \n" << modelCfg
        << "\n[FastreidConfig]: \n" << reidCfg << std::endl;

    Baseline baseline{modelCfg, reidCfg}; 

    if (argc == 2 && std::string(argv[1]) == "-s") {
        auto backbone = createBackbone<IActivationLayer>(reidCfg); 
        if(!backbone) {
            std::cout << "CreateBackbone Failed." << std::endl;
            return -1;
        }
        std::cout << "[Serializling Engine]" << std::endl;
        if(!baseline.serializeEngine<IActivationLayer, IScaleLayer>(ENGINE_PATH, backbone, embedding_head)) {
            std::cout << "SerializeEngine Failed." << std::endl;
            return -1;
        }   
        return 0;
    } else if (argc == 2 && std::string(argv[1]) == "-d") {
        std::cout << "[Deserializling Engine]" << std::endl;
        if(!baseline.deserializeEngine(ENGINE_PATH)) {
            std::cout << "DeserializeEngine Failed." << std::endl;
            return -1;
        }

/* comment out(//#define VERIFY) for real images usage */
#define VERIFY

#ifdef VERIFY   
        /* support batch input data */
        std::vector<cv::Mat> input;

        input.emplace_back(cv::Mat(INPUT_H, INPUT_W, CV_8UC3, cv::Scalar(255,255,255))); // batch size = 1
        //input.emplace_back(cv::Mat(INPUT_H, INPUT_W, CV_8UC3, cv::Scalar(255,255,255))); // batch size = 2, ...

        /* run inference */
        TimePoint start_infer, end_infer;
        int LOOP_TIMES = 100;
        start_infer = Time::now();
        for (int times = 0; times < LOOP_TIMES; ++times) {
            if(!baseline.inference(input)) {
                std::cout << "Inference Failed." << std::endl;
                return -1;
            }
        }
        end_infer = Time::now();

        /* get output from cudaMallocHost */
        float* feat_embedding = baseline.getOutput();

        /* print output */
        TRTASSERT(feat_embedding);
        for (size_t img_idx = 0; img_idx < input.size(); ++img_idx) {
            for (int dim = 0; dim < baseline.getOutputSize(); ++dim) {
                std::cout<< feat_embedding[img_idx+dim] << " ";
                if ((dim+1) % 10 == 0) {
                    std::cout << std::endl;
                }
            }
        }
        std::cout << std::endl;
        
        /* Not including image resizing */
        std::cout << "[Preprocessing+Inference]: " << 
            std::chrono::duration_cast<std::chrono::milliseconds>(end_infer - start_infer).count()/static_cast<float>(LOOP_TIMES) << "ms" << std::endl;         
#else      
        /* get jpg filenames */
        auto filenames = io::fileGlob("../data/*.jpg"); 
        std::cout << "#filenames: " << filenames.size() << std::endl;
        std::vector<cv::Mat> input;
        for (size_t batch_start = 0; batch_start < filenames.size(); batch_start+=modelCfg.max_batch_size) {
            input.clear();
            /* collect batch */
            for (int img_idx = 0; img_idx < modelCfg.max_batch_size; ++img_idx) {
                if ( (batch_start + img_idx) >= filenames.size() ) continue; 
                std::cout << "Image: " << filenames[batch_start + img_idx] << std::endl;
                cv::Mat resizeImg(modelCfg.input_h, modelCfg.input_w, CV_8UC3);
                cv::resize(cv::imread(filenames[batch_start + img_idx]), resizeImg, resizeImg.size(), 0, 0, cv::INTER_CUBIC); /* cv::INTER_LINEAR */
                cv::imwrite("./file_idx[" + std::to_string(batch_start + img_idx) + "].jpg", resizeImg); /* Visualize resize image */
                input.emplace_back(resizeImg);
            }
            if(!baseline.inference(input)) {
                std::cout << "Inference Failed." << std::endl;
                return -1;
            }
        }
#endif
        return 0;
    } else {
        std::cerr << "arguments not right!" << std::endl;
        std::cerr << "./demo/fastrt -s  // serialize model to .engine file" << std::endl;
        std::cerr << "./demo/fastrt -d  // deserialize .engine file and run inference" << std::endl;
        return -1;
    }
}
