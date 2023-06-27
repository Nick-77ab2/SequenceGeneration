/**
 * @mainpage CS361 - Sequence generation
 * @section Description
 * 
 * <Include some description here>
 * 
 * Make commands:
 * 
 *  make
 * 
 * will build the binary.
 * 
 *  make run
 * 
 * will run sequence with 7 numbers and 9 numbers
 * 
 *  make clean
 * 
 * will clear out compiled code.
 * 
 *  make doc
 * 
 * will build the doxygen files.
 */

/**
 * @file
 * @author <Include your name here>
 * @date 2021-2022
 * @section Description
 * 
 * <Include description here>
*/
#include <string>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <memory>
#include <functional>
#include <queue>
#include <typeinfo>
#include <future>
#include <cmath>
namespace fs = std::filesystem;


//Create threadsafe_queue used from the book (Anthony Williams Concurrency in Action) and modified a bit because the book didn't check its code for bool try_pop
template<typename T>
class threadsafe_queue
{
private:
    struct node
    {
        std::shared_ptr<T> data;
        std::unique_ptr<node> next;
    };
    std::mutex head_mutex;
    std::unique_ptr<node> head;
    std::mutex tail_mutex;
    node* tail;
    std::condition_variable data_cond;
    node* get_tail()
    {
        std::lock_guard<std::mutex> tail_lock(tail_mutex);
        return tail;
    }
    std::unique_ptr<node> pop_head() 
    {
        std::unique_ptr<node> old_head=std::move(head);
        head=std::move(old_head->next);
        return old_head;
    }
    std::unique_lock<std::mutex> wait_for_data() 
    {
        std::unique_lock<std::mutex> head_lock(head_mutex);
        data_cond.wait(head_lock,[&]{return head.get()!=get_tail();});
        return std::move(head_lock); 
    }
    std::unique_ptr<node> wait_pop_head()
    {
        std::unique_lock<std::mutex> head_lock(wait_for_data()); 
        return pop_head();
    }
    std::unique_ptr<node> wait_pop_head(T& value)
    {
        std::unique_lock<std::mutex> head_lock(wait_for_data()); 
        value=std::move(*head->data);
        return pop_head();
    }
    std::unique_ptr<node> try_pop_head()
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        if(head.get()==get_tail())
        {
            return std::unique_ptr<node>();
        }
        return pop_head();
    }
    std::unique_ptr<node> try_pop_head(T& value)
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        if(head.get()==get_tail())
        {
            return std::unique_ptr<node>();
        }
        value=std::move(*head->data);
        return pop_head();
    }
public:
    threadsafe_queue():
        head(new node),tail(head.get())
    {}
    threadsafe_queue(const threadsafe_queue& other)=delete;
    threadsafe_queue& operator=(const threadsafe_queue& other)=delete;
    std::shared_ptr<T> try_pop()
    {
        std::unique_ptr<node> old_head=try_pop_head();
        return old_head?old_head->data:std::shared_ptr<T>();
    }
    bool try_pop(T& value)
    {
        bool old_head=try_pop_head(value);
        return old_head;
    }
    std::shared_ptr<T> wait_and_pop()
    {
        std::unique_ptr<node> const old_head=wait_pop_head();
        return old_head->data;
    }
    void wait_and_pop(T& value)
    {
        std::unique_ptr<node> const old_head=wait_pop_head(value);
    }
    void push(T new_value)
    {
        std::shared_ptr<T> new_data(
            std::make_shared<T>(std::move(new_value)));
        std::unique_ptr<node> p(new node);
        {
        std::lock_guard<std::mutex> tail_lock(tail_mutex);
        tail->data=new_data;
        node* const new_tail=p.get();
        tail->next=std::move(p);
        tail=new_tail;
        }
        data_cond.notify_one();
    }
    bool empty()
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        return (head.get()==get_tail());
    }
};
//Joiner for join_threads in thread_pool, courtesy of the book (Anthony Williams Concurrency in Action), necessary for the pool.
class join_threads
{
    std::vector<std::thread>& threads;
public:
    explicit join_threads(std::vector<std::thread>& threads_):
        threads(threads_)
    {}
    ~join_threads()
    {
        for(unsigned long i=0;i<threads.size();++i)
        {
            if(threads[i].joinable())
                threads[i].join();
        }
    }
};
//Thread Pool Class as seen in the book (Anthony Williams Concurrency in Action), put together and modified to work with this.
class thread_pool
{
    std::atomic_bool done;
    threadsafe_queue<std::function<void()> > work_queue;
    std::vector<std::thread> threads; 
    join_threads joiner; 
    void worker_thread()
    {
        while(!done) 
        {
            std::function<void()> task;
            if(work_queue.try_pop(task)) 
            {
                task(); 
            }
            else
            {
                std::this_thread::yield(); 
            }
        }
    }
public:
    thread_pool():
        done(false),joiner(threads)
        {
            unsigned const thread_count=std::thread::hardware_concurrency()-1; 
            try
            {
                for(unsigned i=0;i<thread_count;++i)
                {
                threads.push_back(
                    std::thread(&thread_pool::worker_thread,this)); 
                }
            }
            catch(...)
            {
                done=true; 
                throw;
            }
        }
    ~thread_pool()
    {
    done=true; 
    }
    template<typename FunctionType>
    void submit(FunctionType f)
    {
        work_queue.push(std::function<void()>(f)); 
    }
};
/**
 checkNumber function
 Check an input to see if it's a number
 @param a is the arguments given in the argument list
 @param final is the length of the argument list (or the length given in general as an int)
 @return wasDigit is returned, though in general a boolean is returned.
*/
bool checkNumber(char** a, int final);

/**
 * Producer is used as a thread to find all files of expected types and add to the pool.
 * 
 * @param v vector taken for use to make permutations
 * @param n value given for creating sequences
 * @param length the sequence size denoter (make smaller)
 * @param size another sequence size denoter.
 * @param final the final vector we're adding to.
 * @return no return
 */
void HeapPermute(std::vector<int> &v, int n, int length, int size,  std::vector<std::vector<int>> &final);
/**
 * Producer is used as a thread to find all files of expected types and add to the pool.
 * 
 * @param n the n value given for creating sequences.
 * @param pool reference to the thread pool. necessary for giving work to the threadpool.
 * @param final the vector of vectors of ints that is the list of created potential sequences that pass rules 1-4.
 * @param target the file to save to.
 * @param mut the mutex used for simple I/O locking across all threads.
 * @return no return
 */
void producer(int n,  thread_pool &pool, std::vector<std::vector<int>> &final, std::string target, std::mutex &mut);

/**
 * mod is the modified modulo function necessary for negative values.
 * 
 * @param a the first value in the modulo
 * @param target the second value in the modulo
 * @return int
 */
int mod(double a, double b);

/**
 * determineSequence is the child thread used within the threadpool to do rules 5 and 6 and add to the file.
 * 
 * @param n the n value given for creating sequences.
 * @param length the length of the necessary vector
 * @param vec the vector being passed in for the thread's job.
 * @param target the file to save to.
 * @param cLock the mutex used for simple I/O locking across all threads.
 * @param numItems the number of items left within the producer that it's pushing out. thread removes 1 from numItems when it's complete.
 * @param vals a parameter of the necessary values left over to check over. saves time.
 * @return no return
 */
void determineSequence(int n, int length, std::vector<int> vec, std::string target, std::mutex &cLock, int &numItems, std::vector<int> vals);

/**
 * @brief 
 * 
 * @param argc 
 * @param argv 
 * @return int 
 */

int main(int argc, char *argv[])
{
    int numbers;
    std::string origNum;
    std::string fileName;
    if(argc==1){
        std::cout<<"Please Enter two arguments i.e. ./bin/sequence 7 seq7.txt"<<std::endl;
        return 0;
    }
    else if(!checkNumber(argv, argc)){
        std::cout<<"Please enter a number for the first input"<<std::endl;
        return 0;
    }
    else if(argc==2){
        std::cout<<"Filename missing. Please enter a filename to write to as well."<<std::endl;
        return 0;
    }
    else if(checkNumber(argv, argc) && (argv[1]=="0" || argv[1]=="1")){
        std::cout<<"Input was invalid (enter a number greater than 1)."<<std::endl;
        return 0;
    }
    else{

        fileName=argv[2];
        origNum=argv[1];
        numbers=stoi(origNum);
        try {
            std::filesystem::remove(fileName);
        }
        catch(const std::filesystem::filesystem_error& err) {
            std::cout << "filesystem error: " << err.what() << '\n';
        }
         //create simple locking mutex for I/O
        std::mutex cLock;
        //create the threadpool
        thread_pool pool;
        //create final thread of vectors
        std::vector<std::vector<int>> final;

        //generate the producer Thread.
        std::thread producerThread(producer, numbers, std::ref(pool), std::ref(final), fileName, std::ref(cLock));
        producerThread.join();
        cLock.lock();
        std::cout<<"!----Wrote sequences to file----!"<<std::endl;
        cLock.unlock();
    }
    return 0;
}

void producer(int n,  thread_pool &pool, std::vector<std::vector<int>> &final, std::string fileName, std::mutex &cLock){
    bool countZero=false;
    int length = (n-1)/2 + 1;
    int numItems=0;
    //generate values for use.
    std::vector<int> vals;
    for(int i=0; i<n; i++){
        vals.push_back(i);
    }
    //create permutations via recursive heap.
    HeapPermute(std::ref(vals), n, n, length, final);
    //create a vector of childValues that more limited for rules 5 and 6 to use.
    std::vector<int> childVals;
    for(int i=2; i<n; i++){
        childVals.push_back(i);
    }
    childVals.erase(std::remove(childVals.begin(), childVals.end(), std::ceil(double(n)/2)), childVals.end());
    //remove the excess numbers in the heap permutation (i.e. reduce 7 to 4)
    for(int i=0; i<final.size(); i++){
        final[i].erase(final[i].begin()+length, final[i].end());
    }
    //sort and then remove duplicates.
    std::sort( final.begin(), final.end() );
    final.erase(unique(final.begin(),final.end()),final.end());
    //set numItems for my sanity check for main prints.
    numItems=final.size();
    //add tasks to the pool.
    for(int i=0; i<final.size(); i++){
        pool.submit(std::bind(determineSequence, n, length, final[i], fileName,std::ref(cLock), std::ref(numItems), childVals));
    }
    //this makes sure that producer thread doesn't stop before the threadpool threads (it does if this isn't here.)
    while(!countZero){
        cLock.lock();
        if(numItems==0){
            countZero=true;
        }
        cLock.unlock();
    }   
}

void determineSequence(int n, int length, std::vector<int> vec, std::string fileName, std::mutex &cLock, int &numItems, std::vector<int> vals){
    //generate pairs
    //rule 5 integers
    int fiveA;
    int fiveB;
    //rule 6 vectors
    std::vector<int> sixA;
    std::vector<int> sixB;
    std::fstream file;
    std::string result;
    //bool for sequence checking
    bool isSequence=true;
    //rule 5 in a quick forloop
    for(int i=0; i<vals.size(); i++){
        fiveA = mod(double(vals[i]), double(n));
        fiveB = mod(double(1-vals[i]), double(n));
        if((std::find(vec.begin(), vec.end(), fiveA) !=vec.end()) && (std::find(vec.begin(), vec.end(), fiveB) !=vec.end())){
            isSequence=false;
        }
    }
    //run all of rule 6 if rule 5 passes
    if(isSequence){
        //creating rule 6 portions
        for(int j=1; j<n; j++){
            sixA.push_back(mod(double(j),double(n)));
            sixA.push_back(mod(double(0-j),double(n)));
        }
        for(int i=0; i<length-1; i++){
            sixB.push_back(mod(double(vec[i+1]-vec[i]),double(n)));
        }
        //sort and check for duplicates
        std::sort(sixB.begin(), sixB.end());
        isSequence = !(std::adjacent_find(sixB.begin(), sixB.end())!=sixB.end());
        //check if values occur in sequence (no pun intended) according to rule 6.
        for(int i=0; i<sixA.size(); i+=2){
            if((std::find(sixB.begin(), sixB.end(), sixA[i]) !=sixB.end()) && (std::find(sixB.begin(), sixB.end(), sixA[i+1]) !=sixB.end())){
                isSequence=false;
            }
        }
    }
    //write (append) to file if it exists. if it doesn't, create it and write to it.
    if(isSequence){
        for(int i=0;i<vec.size()-1;i++){
                result+=std::to_string(vec[i]);
                result+=", ";
            }
            result+=std::to_string(vec[vec.size()-1]);
        cLock.lock();
        file.open(fileName, std::fstream::in | std::fstream::out | std::fstream::app);
        if(!file){
            file.open(fileName, std::fstream::in | std::fstream::out | std::fstream::trunc);
            file<<result<<"\n";
            file.close();
        }
        else{
            file<<result<<"\n";
        }
        cLock.unlock();
    }
    cLock.lock();
    numItems-=1;
    cLock.unlock();
}

// A function swapping values using references.
void swap(int *x, int *y)
{
	int temp;
	temp = *x;
	*x = *y;
	*y = temp;
}

void HeapPermute(std::vector<int> &v, int n, int length, int size,  std::vector<std::vector<int>> &final)
{
	int i;
    std::vector<int> setup(length);
	// Print the sequence if the heap top reaches to the 1.
	if (n == 1){
        for(int i=0; i<length; i++){
            setup[i]=v[i];
        }
        if(setup[0]==0 && setup[size-1]==1){
            bool found=false;
            for(int i=1; i<size-1; i++){
                if(setup[i]==int(std::ceil((double(length)/2)))){
                    found=true;
                }
            }
            if(!found)
                final.push_back(setup);
        }
        setup.clear();
    }
	else
	{
		// Fix a number at the heap top until only two one element remaining and permute remaining.
		for (i = 0; i < n; i++)
		{
			HeapPermute(v, n-1, length, size, final);
			// If odd then swap the value at the start index with the n-1.
			if(n%2 == 1)
				swap(&v[0], &v[n-1]);
			// If even then swap the value at the 'i' index with the n-1.
			else
				swap(&v[i], &v[n-1]);
		}
	}
}

//checking if the inputs are numbers.
bool checkNumber(char** a, int final){
    bool wasDigit=true;
    std::string first = a[1];
    //check if that input is actually a number.
    for(size_t i=0; i< first.length(); i++ )
    {
        if(isdigit(first[i])){
            wasDigit=true;
            continue;
        }
        else{
            //leave loop if it's not a number
            wasDigit=false;
            break;
        }
    } 
    return wasDigit;
}

int mod(double a, double b){
    int r;
    if(b==0){
        throw std::invalid_argument("Bad Input!");
    }
    if(a>=0){
        int q = std::floor(a/b);
        r = a - q * b;
        return r;
    }
    r = mod(std::abs(a),b);
    if(r!=0){
        r = b - r;
    }
    return r;
}