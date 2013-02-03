#include "DistributeFileSyncMgr.h"
#include "NodeManagerBase.h"
#include "SuperNodeManager.h"
#include "RequestLog.h"
#include "RecoveryChecker.h"
#include <net/distribute/DataTransfer2.hpp>
#include <configuration-manager/CollectionPath.h>
#include <sf1r-net/RpcServerConnection.h>
#include <iostream>
#include <boost/shared_ptr.hpp>
#include <glog/logging.h>
#include <boost/filesystem.hpp>
#include <boost/crc.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace bfs = boost::filesystem;
namespace ba = boost::asio;
using namespace boost::posix_time;

namespace sf1r
{

static void doTransferFile(const ReadyReceiveData& reqdata)
{
    izenelib::net::distribute::DataTransfer2 transfer(reqdata.receiver_ip, reqdata.receiver_port);
    FinishReceiveRequest req;
    if (not transfer.syncSend(reqdata.filepath, reqdata.filepath, false))
    {
        LOG(INFO) << "transfer file failed: " << reqdata.filepath;
        req.param_.success = false;
    }
    else
    {
        LOG(INFO) << "transfer file success: " << reqdata.filepath;
        req.param_.success = true;
    }
    req.param_.filepath = reqdata.filepath;
    DistributeFileSyncMgr::get()->sendFinishNotifyToReceiver(reqdata.receiver_rpcip, reqdata.receiver_rpcport, req);
}

static void getFileList(const std::string& dir, std::vector<std::string>& file_list,
    const std::set<std::string>& ignore_list, bool recrusive)
{
    static const bfs::directory_iterator end_it = bfs::directory_iterator();
    for( bfs::directory_iterator file(dir); file != end_it; ++file )
    {
        bfs::path current(file->path());
        if (bfs::is_directory(current))
        {
            if (recrusive)
                getFileList(current.string(), file_list, ignore_list, recrusive);
        }
        else if (bfs::is_regular_file(current))
        {
            if (ignore_list.find(current.filename().string()) != ignore_list.end())
            {
                LOG(INFO) << "ignore checking file: " << current;
                continue;
            }
            file_list.push_back(current.string());
        }
        else
        {
            LOG(INFO) << "checking ignore file : " << current;
        }
    }
}

static uint32_t getFileCRC(const std::string& file)
{
    if (!bfs::exists(file))
        return 0;
    boost::crc_32_type crc_computer;
    static const size_t bufsize = 1024*512;
    char *buf = new char[bufsize];
    try
    {
        ifstream ifs(file.c_str(), ios::binary);
        size_t readed = 0;
        while(ifs.good())
        {
            ifs.read(buf, bufsize);
            readed = ifs.gcount();
            crc_computer.process_bytes(buf, readed);
            if ((readed == 0) && ifs.eof())
                break;
        }
    }
    catch(const std::exception& e)
    {
        LOG(INFO) << "error while get crc for file:" << file << ", err:" << e.what();
        delete[] buf;
        return 0;
    }
    delete[] buf;
    return crc_computer.checksum();
}

static void doReportStatus(const ReportStatusReqData& reqdata)
{
    //
    ReportStatusRsp rsp_req;
    rsp_req.param_.rsp_host = SuperNodeManager::get()->getLocalHostIP();
    rsp_req.param_.success = true;
    rsp_req.param_.check_file_result.resize(reqdata.check_file_list.size());
    for (size_t i = 0; i < reqdata.check_file_list.size(); ++i)
    {
        const std::string& file = reqdata.check_file_list[i];
        rsp_req.param_.check_file_result[i] = boost::lexical_cast<std::string>(getFileCRC(file));
        //LOG(INFO) << "file : " << file << ", checksum:" << rsp_req.param_.check_file_result[i];
    }

    DistributeFileSyncMgr::get()->sendReportStatusRsp(reqdata.req_host, SuperNodeManager::get()->getFileSyncRpcPort(), rsp_req);
}

FileSyncServer::FileSyncServer(const std::string& host, uint16_t port, uint32_t threadNum)
    : host_(host)
    , port_(port)
    , threadNum_(threadNum)
    , threadpool_(threadNum_)
{
}

FileSyncServer::~FileSyncServer()
{
    std::cout << "~FileSyncServer()" << std::endl;
    stop();
}

void FileSyncServer::start()
{
    instance.listen(host_, port_);
    instance.start(threadNum_);
    LOG(INFO) << "starting file sync server on : " << host_ << ":" << port_;
}

void FileSyncServer::join()
{
    instance.join();
}

void FileSyncServer::run()
{
    start();
    join();
}

void FileSyncServer::stop()
{
    instance.end();
    instance.join();
}

void FileSyncServer::dispatch(msgpack::rpc::request req)
{
    try
    {
        std::string method;
        req.method().convert(&method);

        if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_TEST])
        {
            msgpack::type::tuple<bool> params;
            req.params().convert(&params);
            req.result(true);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_GET_REQLOG])
        {
            msgpack::type::tuple<GetReqLogData> params;
            req.params().convert(&params);
            GetReqLogData& reqdata = params.get<0>();
            // get 100 new logs at most each time
            //
            reqdata.success = false;
            boost::shared_ptr<ReqLogMgr> reqlogmgr = RecoveryChecker::get()->getReqLogMgr();
            ReqLogHead head;
            size_t headoffset;
            std::string req_packed_data;
            bool ret = reqlogmgr->getHeadOffset(reqdata.start_inc, head, headoffset);
            if (!ret)
            {
                LOG(INFO) << "no more log after inc_id : " << reqdata.start_inc;
            }
            else
            {
                reqdata.logdata_list.reserve(100);
                LOG(INFO) << "get log from inc_id : " << reqdata.start_inc;
                while(true)
                {
                    ret = reqlogmgr->getReqDataByHeadOffset(headoffset, head, req_packed_data);
                    if(!ret || reqdata.logdata_list.size() > 100)
                        break;
                    reqdata.logdata_list.push_back(req_packed_data);
                    reqdata.end_inc = head.inc_id;
                }
                LOG(INFO) << "get log last inc_id :" << head.inc_id;
            }
            
            reqdata.success = true;
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_GET_SCD_LIST])
        {
            msgpack::type::tuple<GetSCDListData> params;
            req.params().convert(&params);
            GetSCDListData& reqdata = params.get<0>();
            // get all the scd file in the backup directory.
            reqdata.success = false;
            CollectionPath colpath;
            bool ret = RecoveryChecker::get()->getCollPath(reqdata.collection, colpath);
            if (ret)
            {
                reqdata.success = true;
                bfs::path backup_path = colpath.getScdPath();
                backup_path /= bfs::path("index/backup/");
                if (bfs::exists(backup_path))
                {
                    static bfs::directory_iterator end_dir = bfs::directory_iterator();
                    bfs::directory_iterator backup_dirs_it = bfs::directory_iterator(backup_path);
                    while (backup_dirs_it != end_dir)
                    {
                        if (bfs::is_regular_file(*backup_dirs_it))
                        {
                            reqdata.scd_list.push_back((*backup_dirs_it).path().string());
                            LOG(INFO) << "find backup scd file : " << (*backup_dirs_it).path().string();
                        }
                        ++backup_dirs_it;
                    }
                }
            }
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_GET_FILE])
        {
            msgpack::type::tuple<GetFileData> params;
            req.params().convert(&params);
            GetFileData& reqdata = params.get<0>();
            reqdata.success = false;
            // get the file info 
            if (bfs::exists(reqdata.filepath) && bfs::is_regular_file(reqdata.filepath))
            {
                reqdata.success = true;
                reqdata.filesize = bfs::file_size(reqdata.filepath);
            }
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_READY_RECEIVE])
        {
            msgpack::type::tuple<ReadyReceiveData> params;
            req.params().convert(&params);
            ReadyReceiveData& reqdata = params.get<0>();
            reqdata.success = true;
            // start transfer thread.
            threadpool_.schedule(boost::bind(&doTransferFile, reqdata));
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_FINISH_RECEIVE])
        {
            msgpack::type::tuple<FinishReceiveData> params;
            req.params().convert(&params);
            FinishReceiveData& reqdata = params.get<0>();
            reqdata.success = true;
            DistributeFileSyncMgr::get()->notifyFinishReceive(reqdata.filepath);
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_REPORT_STATUS_REQ])
        {
            msgpack::type::tuple<ReportStatusReqData> params;
            req.params().convert(&params);
            ReportStatusReqData& reqdata = params.get<0>();
            threadpool_.schedule(boost::bind(&doReportStatus, reqdata));
            req.result(true);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_REPORT_STATUS_RSP])
        {
            msgpack::type::tuple<ReportStatusRspData> params;
            req.params().convert(&params);
            ReportStatusRspData& rspdata = params.get<0>();
            DistributeFileSyncMgr::get()->notifyReportStatusRsp(rspdata);
            req.result(true);
        }
        else
        {
            req.error(msgpack::rpc::NO_METHOD_ERROR);
        }
    }
    catch (const msgpack::type_error& e)
    {
        req.error(msgpack::rpc::ARGUMENT_ERROR);
    }
    catch (const std::exception& e)
    {
        req.error(std::string(e.what()));
    }
}

DistributeFileSyncMgr::DistributeFileSyncMgr()
{
    conn_mgr_ = new RpcServerConnection();
    RpcServerConnectionConfig config;
    config.rpcThreadNum = 4;
    conn_mgr_->init(config);
    ignore_list_.insert("LOG");
    ignore_list_.insert("LOG.old");
    ignore_list_.insert("cookie");
    reporting_ = false;
}

void DistributeFileSyncMgr::init()
{
    if (!NodeManagerBase::get()->isDistributed())
        return;
    transfer_rpcserver_.reset(new FileSyncServer(SuperNodeManager::get()->getLocalHostIP(),
            SuperNodeManager::get()->getFileSyncRpcPort(), 4));
    transfer_rpcserver_->start();
}

DistributeFileSyncMgr::~DistributeFileSyncMgr()
{
    delete conn_mgr_;
    if (transfer_rpcserver_)
        transfer_rpcserver_->stop();
}

void DistributeFileSyncMgr::notifyReportStatusRsp(const ReportStatusRspData& rspdata)
{
    //
    boost::unique_lock<boost::mutex> lk(status_report_mutex_);
    status_rsp_list_.push_back(rspdata);
    status_report_cond_.notify_all();
}

void DistributeFileSyncMgr::sendReportStatusRsp(const std::string& ip, uint16_t port, const ReportStatusRsp& rsp)
{
    bool rsp_ret = false;
    try
    {
        conn_mgr_->syncRequest(ip, port, rsp, rsp_ret);
    }
    catch(const std::exception& e)
    {
        LOG(ERROR) << "send report status response failed.";
    }
}

void DistributeFileSyncMgr::checkReplicasStatus(const std::string& colname, std::string& check_errinfo)
{
    if (!NodeManagerBase::get()->isDistributed())
        return;

    CollectionPath colpath;
    bool ret = RecoveryChecker::get()->getCollPath(colname, colpath);
    if (!ret)
    {
        check_errinfo = "check collection not exist for  " + colname;
        LOG(INFO) << check_errinfo;
        return;
    }
    {
        boost::unique_lock<boost::mutex> lk(status_report_mutex_);
        if (reporting_)
        {
            check_errinfo = "there is already a report request processing.";
            LOG(INFO) << check_errinfo;
            return;
        }
        reporting_ = true;
    }
    std::vector<std::string> replica_info;
    NodeManagerBase::get()->getAllReplicaInfo(replica_info, true);
    uint16_t port = SuperNodeManager::get()->getFileSyncRpcPort();

    ReportStatusRequest req;
    req.param_.req_host = SuperNodeManager::get()->getLocalHostIP();

    getFileList(colpath.getCollectionDataPath(), req.param_.check_file_list, ignore_list_, true);
    LOG(INFO) << "checking got file num :" << req.param_.check_file_list.size();
    std::vector<std::string> file_checksum_list(req.param_.check_file_list.size());

    // calculate local.
    for (size_t i = 0; i < req.param_.check_file_list.size(); ++i)
    {
        const std::string& file = req.param_.check_file_list[i];
        file_checksum_list[i] = boost::lexical_cast<std::string>(getFileCRC(file));
        //LOG(INFO) << "file : " << file << ", checksum:" << file_checksum_list[i];
    }

    size_t self = 0;
    int wait_num = 0;
    for( size_t i = 0; i < replica_info.size(); ++i)
    {
        if (replica_info[i] == SuperNodeManager::get()->getLocalHostIP())
        {
            self = i;
            continue;
        }
        bool rsp_ret = false;
        try
        {
            conn_mgr_->syncRequest(replica_info[i], port, req, rsp_ret);
        }
        catch(const std::exception& e)
        {
            LOG(INFO) << "send request error while checking status: " << e.what();
            continue;
        }
        if (rsp_ret)
            ++wait_num;
    }
    int max_wait = 10;
    // wait for response.
    while(wait_num > 0)
    {
        std::vector<ReportStatusRspData> rspdata;
        {
            boost::unique_lock<boost::mutex> lk(status_report_mutex_);
            while (status_rsp_list_.empty())
            {
                if (--max_wait < 0)
                {
                    LOG(INFO) << "wait max time!! no longer wait, no rsp num: " << wait_num;
                    reporting_ = false;
                    check_errinfo = "wait report status timeout!!";
                    return;
                }
                status_report_cond_.timed_wait(lk, boost::posix_time::seconds(10));
            }
            // reset wait time if got any rsp.
            max_wait = 10;
            rspdata.swap(status_rsp_list_);
        }
        LOG(INFO) << "status report got rsp: " << rspdata.size();
        for(size_t i = 0; i < rspdata.size(); ++i)
        {
            LOG(INFO) << "checking rsp for host :" << rspdata[i].rsp_host;
            if (!rspdata[i].success)
            {
                LOG(WARNING) << "rsp return false from this host!!";
                continue;
            }
            if (rspdata[i].check_file_result.size() != file_checksum_list.size())
            {
                LOG(WARNING) << "rsp file check result size if not matched!!!!";
                continue;
            }
            for (size_t j = 0; j < file_checksum_list.size(); ++j)
            {
                if (file_checksum_list[j] != rspdata[i].check_file_result[j])
                {
                    LOG(WARNING) << "one of file not the same as local : " << req.param_.check_file_list[j];
                    check_errinfo = "at least one of file not the same status between replicas.";
                }
            }
        }
        wait_num -= rspdata.size();
    }
    LOG(INFO) << "report request finished";
    boost::unique_lock<boost::mutex> lk(status_report_mutex_);
    reporting_ = false;
}

bool DistributeFileSyncMgr::getNewestReqLog(uint32_t start_from, std::vector<std::string>& saved_log)
{
    if (!NodeManagerBase::get()->isDistributed())
        return true;
    // note : for optimize, we can get log from any node that has entered the cluster and 
    // current state is ready.
    int retry = 3;

    srand( time(NULL) );

    while(retry-- > 0)
    {
        std::string ip;
        // because all replica node should have the same config,
        // so the rpc ports for all file sync servers are the same.
        uint16_t port = SuperNodeManager::get()->getFileSyncRpcPort();

        if(!NodeManagerBase::get()->getCurrNodeSyncServerInfo(ip, rand()))
        {
            LOG(INFO) << "get file sync server failed. This may happen if only one sf1r node.";
            return false;
        }
        LOG(INFO) << "try get newest log from: " << ip << ":" << port;
        GetReqLogRequest req;
        req.param_.start_inc = start_from;
        GetReqLogData rsp;

        try
        {
            conn_mgr_->syncRequest(ip, port, req, rsp);
        }
        catch(const std::exception& e)
        {
            LOG(INFO) << "send request error, will retry : " << e.what();
            continue;
        }
        if (rsp.success)
        {
            saved_log.swap(rsp.logdata_list);
            return true;
        }
    }
    LOG(INFO) << "get newest log data failed .";
    return false;
}

bool DistributeFileSyncMgr::syncNewestSCDFileList(const std::string& colname)
{
    if (!NodeManagerBase::get()->isDistributed())
        return true;
    // get the backup scd file list used for index.
    
    // get primary file sync ip:port
    //
    // send rpc request to get scd filelist
    //
    // receiver response that will contain the filelist.
    // for each file in list, send rpc request to get file info,
    // if file is the same as local, we think it already exist and
    // continue to get next.
    int retry = 3;
    srand( time(NULL) );

    while(retry-- > 0)
    {
        std::string ip;
        uint16_t port = SuperNodeManager::get()->getFileSyncRpcPort();
        if(!NodeManagerBase::get()->getCurrNodeSyncServerInfo(ip, rand()))
        {
            LOG(INFO) << "get file sync server failed. This may happen if only one sf1r node.";
            return false;
        }
        LOG(INFO) << "try get scd file list from: " << ip << ":" << port;
        GetSCDListRequest req;
        req.param_.collection = colname;
        GetSCDListData rsp;
        try
        {
            conn_mgr_->syncRequest(ip, port, req, rsp);
        }
        catch(const std::exception& e)
        {
            LOG(INFO) << "send request error, will retry : " << e.what();
            continue;
        }

        if (rsp.success)
        {
            if (rsp.scd_list.empty())
            {
                LOG(INFO) << "no scd file need sync.";
                return true;
            }
            for (size_t i = 0; i < rsp.scd_list.size(); ++i)
            {
                GetFileData file_rsp;
                file_rsp.filepath = rsp.scd_list[i];
                if (!getFileInfo(ip, port, file_rsp))
                    break;

                if(!getFileFromOther(ip, port, file_rsp.filepath, file_rsp.filesize))
                {
                    LOG(INFO) << "get file from other failed, retry next." << file_rsp.filepath;
                    break;
                }
                LOG(INFO) << "a scd file finished :" << file_rsp.filepath;
                if (i == rsp.scd_list.size() - 1)
                    return true;
            }
        }
    }
    LOG(INFO) << "sync to newest scd file list failed.";
    return false;
}

bool DistributeFileSyncMgr::pushFileToAllReplicas(const std::string& srcpath,
    const std::string& destpath, bool recrusive)
{
    if (!NodeManagerBase::get()->isDistributed())
        return true;
    std::vector<std::string> replica_info;
    NodeManagerBase::get()->getAllReplicaInfo(replica_info);
    bool all_success = true;
    uint16_t port = SuperNodeManager::get()->getDataReceiverPort();
    for (size_t i = 0; i < replica_info.size(); ++i)
    {
        izenelib::net::distribute::DataTransfer2 transfer(replica_info[i], port);
        if (not transfer.syncSend(srcpath, destpath, recrusive))
        {
            LOG(WARNING) << "push file to replica failed: " << replica_info[i];
            all_success = false;
        }
        LOG(INFO) << "push file to replica success : " << replica_info[i];
    }
    return all_success;
}

bool DistributeFileSyncMgr::getFileInfo(const std::string& ip, uint16_t port, GetFileData& file_rsp)
{
    LOG(INFO) << "try get file from: " << ip << ":" << port;
    GetFileRequest file_req;
    file_req.param_.filepath = file_rsp.filepath;
    try
    {
        conn_mgr_->syncRequest(ip, port, file_req, file_rsp);
    }
    catch(const std::exception& e)
    {
        LOG(INFO) << "send request error while get file info : " << e.what();
        return false;
    }
    if (!file_rsp.success)
    {
        LOG(INFO) << "get file info failed for :" << file_rsp.filepath;
        return false;
    }
    return true;
}

bool DistributeFileSyncMgr::getFileFromOther(const std::string& filepath, bool force_overwrite)
{
    int retry = 3;
    srand( time(NULL) );

    while(retry-- > 0)
    {
        std::string ip;
        uint16_t port = SuperNodeManager::get()->getFileSyncRpcPort();
        if(!NodeManagerBase::get()->getCurrNodeSyncServerInfo(ip, rand()))
        {
            LOG(INFO) << "get file sync server failed. This may happen if only one sf1r node.";
            return false;
        }
        GetFileData file_rsp;
        file_rsp.filepath = filepath;
        if (!getFileInfo(ip, port, file_rsp))
            continue;

        if(!getFileFromOther(ip, port, file_rsp.filepath, file_rsp.filesize, force_overwrite))
        {
            LOG(INFO) << "get file from other failed, retry next." << file_rsp.filepath;
            continue;
        }

        LOG(INFO) << "get file finished :" << file_rsp.filepath;
        return true;
    }
    return false;
}

bool DistributeFileSyncMgr::getFileFromOther(const std::string& ip, uint16_t port,
    const std::string& filepath, uint64_t filesize, bool force_overwrite)
{
    if (!transfer_rpcserver_)
        return false;

    if (bfs::exists(filepath))
    {
        if (!bfs::is_regular_file(filepath))
        {
            LOG(INFO) << "found a file with same name but not a regular file." << filepath;
            bfs::remove_all(filepath + "_renamed");
            bfs::rename(filepath, filepath + "_renamed");
        }
        else
        {
            if(bfs::file_size(filepath) == filesize)
            {
                LOG(INFO) << "local file is the same size : " << filepath;
                if (!force_overwrite)
                    return true;
            }
            bfs::remove(filepath);
        }
    }

    ReadyReceiveRequest req;
    req.param_.receiver_ip = SuperNodeManager::get()->getLocalHostIP();
    req.param_.receiver_port = SuperNodeManager::get()->getDataReceiverPort();
    req.param_.receiver_rpcip = transfer_rpcserver_->getHost();
    req.param_.receiver_rpcport = transfer_rpcserver_->getPort();
    req.param_.filepath = filepath;
    req.param_.filesize = filesize;

    ReadyReceiveData rsp;
    try
    {
        conn_mgr_->syncRequest(ip, port, req, rsp);
    }
    catch(const std::exception& e)
    {
        LOG(INFO) << "send request error while get file from other: " << e.what();
        return false;
    }

    // wait receiver.
    return waitFinishReceive(filepath, filesize);
}

void DistributeFileSyncMgr::sendFinishNotifyToReceiver(const std::string& ip, uint16_t port, const FinishReceiveRequest& req)
{
    FinishReceiveData rsp;
    try
    {
        conn_mgr_->syncRequest(ip, port, req, rsp);
    }
    catch(const std::exception& e)
    {
        LOG(ERROR) << "send request error while notify finish to other: " << e.what();
    }
}

void DistributeFileSyncMgr::notifyFinishReceive(const std::string& filepath)
{
    boost::unique_lock<boost::mutex> lk(mutex_);
    if (wait_finish_notify_.find(filepath) == wait_finish_notify_.end())
    {
        LOG(INFO) << "a file finish notify but not waiting: " << filepath;
        return;
    }
    wait_finish_notify_[filepath] = true;
    cond_.notify_all();
    LOG(INFO) << "a file finish notify for : " << filepath;
}

bool DistributeFileSyncMgr::waitFinishReceive(const std::string& filepath, uint64_t filesize)
{
    boost::unique_lock<boost::mutex> lk(mutex_);
    if (wait_finish_notify_.find(filepath) != wait_finish_notify_.end())
    {
        LOG(INFO) << "file receiver is already waiting : " << filepath;
        return false;
    }
    wait_finish_notify_[filepath] = false;
    while(!wait_finish_notify_[filepath])
    {
        cond_.wait(lk);
    }
    LOG(INFO) << "a file finished receive : " << filepath;
    wait_finish_notify_.erase(filepath);
    if (bfs::exists(filepath) && bfs::is_regular_file(filepath) && bfs::file_size(filepath) == filesize)
        return true;
    LOG(WARNING) << "file failed pass check: " << filepath;
    return false;
}

}
