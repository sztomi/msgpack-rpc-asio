#pragma once

namespace msgpack {
namespace rpc {
namespace asio {


class request_factory
{
    ::msgpack::rpc::msgid_t m_next_msgid;
public:
    request_factory()
        : m_next_msgid(1)
        {
        }

    ::msgpack::rpc::msgid_t next_msgid()
    {
        return m_next_msgid++;
    }

    // 0
    ::msgpack::rpc::msg_request<std::string, std::tuple<>> 
        create(const std::string &method)
        {
            ::msgpack::rpc::msgid_t msgid = next_msgid();
            typedef std::tuple<> Parameter;
            return ::msgpack::rpc::msg_request<std::string, Parameter>(
                    method, Parameter(), msgid);
        }
    // 1
    template<typename A1>
        ::msgpack::rpc::msg_request<std::string, std::tuple<A1>> 
        create(const std::string &method, A1 a1)
        {
            ::msgpack::rpc::msgid_t msgid = next_msgid();
            typedef std::tuple<A1> Parameter;
            return ::msgpack::rpc::msg_request<std::string, Parameter>(
                    method, Parameter(a1), msgid);
        }
    // 2
    template<typename A1, typename A2>
        ::msgpack::rpc::msg_request<std::string, std::tuple<A1, A2>> 
        create(const std::string &method, A1 a1, A2 a2)
        {
            ::msgpack::rpc::msgid_t msgid = next_msgid();
            typedef std::tuple<A1, A2> Parameter;
            return ::msgpack::rpc::msg_request<std::string, Parameter>(
                    method, Parameter(a1, a2), msgid);
        }
};


class func_call
{
public:
    enum STATUS_TYPE
    {
        STATUS_WAIT,
        STATUS_RECEIVED,
        STATUS_ERROR,
    };
private:
    STATUS_TYPE m_status;
    ::msgpack::object m_result;
    ::msgpack::object m_error;
    std::string m_request;
    boost::mutex m_mutex;
    boost::condition_variable_any m_cond;
public:
    func_call(const std::string &s)
        : m_status(STATUS_WAIT), m_request(s)
        {
        }

    void set_result(const ::msgpack::object &result)
    {
        if(m_status!=STATUS_WAIT){
            throw rpc_error("already finishded");
        }
        boost::mutex::scoped_lock lock(m_mutex);
        m_result=result;
        m_status=STATUS_RECEIVED;
        m_cond.notify_all();
    }

    void set_error(const ::msgpack::object &error)
    {
        if(m_status!=STATUS_WAIT){
            throw rpc_error("already finishded");
        }
        boost::mutex::scoped_lock lock(m_mutex);
        m_error=error;
        m_status=STATUS_ERROR;
        m_cond.notify_all();
    }

    // blocking
    func_call& sync()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        if(m_status==STATUS_WAIT){
            m_cond.wait(m_mutex);
        }
        return *this;
    }

    template<typename R>
        R& convert(R *value)const
        {
            if(m_status==STATUS_RECEIVED){
                m_result.convert(value);
                return *value;
            }
            else{
                throw rpc_error("not ready");
            }
        }

    std::string string()const
    {
        std::stringstream ss;
        ss << m_request << " = ";
        switch(m_status)
        {
            case func_call::STATUS_WAIT:
                ss << "?";
                break;
            case func_call::STATUS_RECEIVED:
                ss << m_result;
                break;
            case func_call::STATUS_ERROR:
                ss << "!";
                break;
            default:
                ss << "!?";
                break;
        }

        return ss.str();
    }
};
inline std::ostream &operator<<(std::ostream &os, const func_call &request)
{
    os << request.string();
    return os;
}


class client
{
	boost::asio::io_service &m_io_service;
    request_factory m_request_factory;

    std::shared_ptr<session> m_session;
    std::map<msgpack::rpc::msgid_t, std::shared_ptr<func_call>> m_request_map;

public:
    client(boost::asio::io_service &io_service)
		: m_io_service(io_service)
    {
	}

    void connect_async(const boost::asio::ip::tcp::endpoint &endpoint)
    {
		auto c=this;
		m_session=session::create(m_io_service, [c](const object &msg, std::shared_ptr<session> session)
		{
			c->receive(msg, session);
		});
        m_session->connect_async(endpoint);
    }

    // 0
    std::shared_ptr<func_call> call_async(const std::string &method)
    {
        auto request=m_request_factory.create(method);
        return send_async(request);
    }
    // 1
    template<typename A1>
        std::shared_ptr<func_call> call_async(const std::string &method, A1 a1)
        {
            auto request=m_request_factory.create(method, a1);
            return send_async(request);
        }
    // 2
    template<typename A1, typename A2>
        std::shared_ptr<func_call> call_async(const std::string &method, A1 a1, A2 a2)
        {
            auto request=m_request_factory.create(method, a1, a2);
            return send_async(request);
        }

    // 0
    template<typename R>
        R &call_sync(R *value, const std::string &method)
        {
            auto request=m_request_factory.create(method);
            auto call=send_async(request);
            call->sync().convert(value);
            return *value;
        }
    // 1
    template<typename R, typename A1>
        R &call_sync(R *value, const std::string &method, A1 a1)
        {
            auto request=m_request_factory.create(method, a1);
            auto call=send_async(request);
            call->sync().convert(value);
            return *value;
        }
    // 2
    template<typename R, typename A1, typename A2>
        R &call_sync(R *value, const std::string &method, A1 a1, A2 a2)
        {
            auto request=m_request_factory.create(method, a1, a2);
            auto call=send_async(request);
            call->sync().convert(value);
            return *value;
        }

private: 
    template<typename Parameter>
        std::shared_ptr<func_call> send_async(const ::msgpack::rpc::msg_request<std::string, Parameter> &msgreq)
        {
            auto sbuf=std::make_shared<msgpack::sbuffer>();
            ::msgpack::pack(*sbuf, msgreq);

            std::stringstream ss;
            ss << msgreq.method << msgreq.param;
            auto req=std::make_shared<func_call>(ss.str());
            m_request_map.insert(std::make_pair(msgreq.msgid, req));

            m_session->enqueue_write(sbuf);

            return req;
        }

private:
	void receive(const object &msg, std::shared_ptr<session> session)
	{
		::msgpack::rpc::msgid_t msgid=0;
		try{
			::msgpack::rpc::msg_rpc rpc;
			msg.convert(&rpc);

			switch(rpc.type) {
			case ::msgpack::rpc::REQUEST: 
				break;

			case ::msgpack::rpc::RESPONSE: 
				{
					::msgpack::rpc::msg_response<object, object> res;
					msg.convert(&res);
					auto found=m_request_map.find(res.msgid);
					if(found!=m_request_map.end()){
						// ToDo: error check
						found->second->set_result(res.result);
					}
					else{
						// ToDo:error
						int a=0;
					}
				}
				break;

			case ::msgpack::rpc::NOTIFY: 
				{
					::msgpack::rpc::msg_notify<object, object> req;
					msg.convert(&req);
					//req.method, req.param);
				}
				break;

			default:
				throw msgpack::rpc::rpc_error("rpc type error");
			}

		}
		catch(::msgpack::rpc::rpc_error ex){
			//shared->enqueue(shared->create_error_message(msgid, ex));
		}
		catch(...){
			//shared->enqueue(shared->create_error_message(msgid, ::msgpack::rpc::rpc_error("unknown error")));
		}
	}
};


}}}
