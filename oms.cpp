#include <curl/curl.h>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <bits/stdc++.h>

using namespace std;

using json = nlohmann::json;

mutex log_mutex;

class ThreadPool {
private:
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable condition;
    bool stop;

public:
    ThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    function<void()> task;

                    {
                        unique_lock<mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });
                        if (this->stop && this->tasks.empty())
                            return;
                        task = move(this->tasks.front());
                        this->tasks.pop();
                    }

                    task();
                }
            });
        }
    }

    template <class F>
    void enqueue(F&& f) {
        {
            unique_lock<mutex> lock(queue_mutex);
            tasks.emplace(forward<F>(f));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (thread& worker : workers)
            worker.join();
    }
};

ofstream log_file("latency_log.txt", ios::app);
void measure_and_log_latency(string operation_name, string elapsed_time) {
    
    cout << "Latency for " << operation_name << ": " << elapsed_time << " microseconds" << endl;

    // adding the latency log statements to the log file
    if (log_file.is_open()) {
        log_file << "Operation: " << operation_name
                 << ", Latency: " << elapsed_time << " microseconds"
                 << ", Timestamp: " << chrono::system_clock::to_time_t(chrono::system_clock::now())
                 << endl;
    } else {
        cerr << "Failed to open log file!" << endl;
    }
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

//access to the log file must not be shared by multiple threads at the same time so we safeguard it using mutex locks
void log_latency_safe(const string& operation_name, const string& elapsed_time) {
    lock_guard<mutex> lock(log_mutex);
    measure_and_log_latency(operation_name, elapsed_time);
}
string sendCurlRequest(json payload, string url, string access_token=""){
    string response;
    CURL* curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl){
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        struct curl_slist* headers = nullptr;
        
        string payload_string = payload.dump();
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_string.c_str());
        headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK){
            cerr<<"cURL Error: "<<curl_easy_strerror(res)<<endl;
            curl_easy_cleanup(curl);
            return "";
        }
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        
    }
    return response;
}
string get_access_token(const string& client_id, const string& client_secret) {
    CURL* curl;
    CURLcode res;

    const string url = "https://test.deribit.com/api/v2/public/auth";

    json payload={
        {"id",0},
        {"jsonrpc","2.0"},
        {"method","public/auth"},
        {"params",{
            {"grant_type","client_credentials"},
            {"client_id",client_id},
            {"client_secret",client_secret}
        }}
    };

    string response = sendCurlRequest(payload, url);
    auto responseJson = json::parse(response);
    return responseJson["result"]["access_token"];
}
void place_order(int price, int amount, string access_token, string instrument){
    const string url = "https://test.deribit.com/api/v2/private/buy";
    json payload = {
        {"id",0},
        {"jsonrpc","2.0"},
        {"method","private/buy"},
        {"params",{
            {"instrument_name",instrument},
            {"amount",amount},
            {"type","limit"},
            {"label","bot"},
            {"price",price}
        }}
    };
    string response = sendCurlRequest(payload, url, access_token);
    cout<<response<<endl;

}
void cancel_order(string order_id, string access_token){
    const string url = "https://test.deribit.com/api/v2/private/cancel";
    json payload = {
        {"id",0},
        {"jsonrpc","2.0"},
        {"method","private/cancel"},
        {"params",{
            {"order_id",order_id}
        }}
    };
    string response = sendCurlRequest(payload, url, access_token);
    cout<<response<<endl;
}

void modify_order(string order_id, int amount, int price){
    const string url = "https://test.deribit.com/api/v2/private/edit";
    json payload = {
        {"id",0},
        {"jsonrpc","2.0"},
        {"method","private/edit"},
        {"params",{
            {"order_id",order_id},
            {"amount",amount},
            {"price",price}
        }}
    };
    string response = sendCurlRequest(payload, url);
    cout<<response<<endl;
}

void get_orderbook(string instrument_name, int depth){
    const string url = "https://test.deribit.com/api/v2/public/get_order_book";
    json payload = {
        {"id",0},
        {"jsonrpc","2.0"},
        {"method","public/get_order_book"},
        {"params",{
            {"instrument_name",instrument_name},
            {"depth",depth}
        }}
    };
    string response = sendCurlRequest(payload, url);
    auto response_json = json::parse(response);
    for(auto ask: response_json["result"]["asks"]){
        cout<<"Price "<<ask[0]<<" Amount "<<ask[1]<<endl;
    }
    //cout<<response<<endl;
}

void get_current_positions(string instrument_name, string access_token){
    const string url = "https://test.deribit.com/api/v2/private/get_positions";
    json payload = {
        {"id",0},
        {"jsonrpc","2.0"},
        {"method","private/get_positions"},
        {"params",{
            {"instrument_name",instrument_name}
        }}
    };
    string response = sendCurlRequest(payload, url, access_token);
    auto response_json = json::parse(response);
    if(response_json.contains("error")){
        cout<<response_json["error"]["message"]<<endl;
    }
    else{
        for(auto position: response_json["result"]){
            cout<<"Instrument: "<<position["instrument_name"]<<" Size: "<<position["size"]<<" Average Price: "<<position["average_price"]<<endl;
        }
    }
    //cout<<response<<endl;
}

void get_open_orders(string access_token){
    const string url = "https://test.deribit.com/api/v2/private/get_open_orders";
    json payload = {
        {"id",0},
        {"jsonrpc","2.0"},
        {"method","private/get_open_orders"},
        {"params",{
            {"kind","future"},
            {"type","limit"}
        }}
    };
    string response = sendCurlRequest(payload, url, access_token);
    auto respose_json = json::parse(response);
    if(respose_json.contains("error")){
        cout<<respose_json["error"]["message"]<<endl;
    }
    else if(respose_json["result"].size() == 0){
        cout<<"No open orders"<<endl;
    }
    else{
        for(auto order: respose_json["result"]){
            cout<<"Order ID: "<<order["order_id"]<<" Price: "<<order["price"]<<" Amount: "<<order["amount"]<<" Instrument "<<order["instrument_name"]<<endl;
        }
    }   
}

int main(){
    string client_id = "";
    string client_secret = "";
    string access_token = get_access_token(client_id, client_secret);
    ThreadPool pool(4);
    
    //continous loop that takes in user input and enques the process in the thread pool
    while(true){
        int command;
        cout<<"To place order:1 \nTo cancel order:2 \nTo modify order:3 \nTo get orderbook:4 \nTo get current positions:5 \nGet Open Orders:6 \nTo exit:7"<<endl;
        cin>>command;
        if(command == 1){
            int price, amount;
            string instrument_name;
            cout<<"Enter instrument_name (BTC-PERPETUAL, ETH-PERPETUAL): ";
            cin>>instrument_name;
            cout<<"Enter price: ";
            cin>>price;
            cout<<"Enter amount: ";
            cin>>amount;
            auto start = chrono::high_resolution_clock::now();
            pool.enqueue([start, price, amount, access_token, instrument_name] {
                place_order(price, amount, access_token, instrument_name);
                auto end = chrono::high_resolution_clock::now();
                chrono::duration<double, micro> elapsed = end - start;
                log_latency_safe("place_order", to_string(elapsed.count()));
            });
        }else if(command == 2){
            string order_id;
            cout<<"Enter order_id: ";
            cin>>order_id;
            auto start = chrono::high_resolution_clock::now();
            pool.enqueue([order_id, access_token, start] {
                cancel_order(order_id, access_token);
                auto end = chrono::high_resolution_clock::now();
                chrono::duration<double, micro> elapsed = end - start;
                log_latency_safe("cancel_order", to_string(elapsed.count()));
            });
        }else if(command == 3){
            string order_id;
            int price, amount;
            cout<<"Enter order_id: ";
            cin>>order_id;
            cout<<"Enter price: ";
            cin>>price;
            cout<<"Enter amount: ";
            cin>>amount;
            auto start = chrono::high_resolution_clock::now();

            pool.enqueue([start, order_id, price, amount] {
                modify_order(order_id, amount, price);
                auto end = chrono::high_resolution_clock::now();
                chrono::duration<double, micro> elapsed = end - start;
                log_latency_safe("modify_order", to_string(elapsed.count()));
            });
        }else if(command == 4){
            string instrument_name;
            int depth;
            cout<<"Enter instrument_name (BTC-PERPETUAL, ETH-PERPETUAL): ";
            cin>>instrument_name;
            cout<<"Enter depth: ";
            cin>>depth;
            auto start = chrono::high_resolution_clock::now();

            pool.enqueue([start, instrument_name, depth] {
                get_orderbook(instrument_name, depth);
                auto end = chrono::high_resolution_clock::now();
                chrono::duration<double, micro> elapsed = end - start;
                log_latency_safe("get_orderbook", to_string(elapsed.count()));
            });

        }else if(command == 5){
            string instrument_name;
            cout<<"Enter instrument_name (BTC-PERPETUAL, ETH-PERPETUAL): ";
            cin>>instrument_name;
            auto start = chrono::high_resolution_clock::now();
            pool.enqueue([start, instrument_name, access_token] {
                get_current_positions(instrument_name, access_token);
                auto end = chrono::high_resolution_clock::now();
                chrono::duration<double, micro> elapsed = end - start;
                log_latency_safe("get_current_positions", to_string(elapsed.count()));
            });
        }else if(command == 6){
            auto start = chrono::high_resolution_clock::now();
            pool.enqueue([start, access_token] {
                get_open_orders(access_token);
                auto end = chrono::high_resolution_clock::now();
                chrono::duration<double, micro> elapsed = end - start;
                log_latency_safe("get_open_orders", to_string(elapsed.count()));
            });
        }
        else if(command == 7){
            break;
        }
    }
}