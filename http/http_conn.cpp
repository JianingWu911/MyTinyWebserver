#include "./http_conn.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

/********************全局的静态变量和类内的静态成员函数有什么区别吗？****************************/
locker m_lock;
map<string, string> users;

void Http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}
void Http_conn::init(int sockfd, 
                const sockaddr_in & addr, 
                char* root, 
                int TRIGMode,
                int close_log, 
                const std::string& user, 
                const std::string password, 
                const std::string sqlname) {
    m_sockfd = sockfd; 
    m_address = addr;
    addfd(m_epollfd, sockfd, true, TRIGMode);
    ++m_user_count;
    // 细节，就不过分看了
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;
    strcpy(sql_user, user.c_str());
    strcpy(sql_password, password.c_str());
    strcpy(sql_name, sqlname.c_str());
    init();
}

void Http_conn::close_conn() {
    /*为了避免重复关闭，将关闭后的m_sockfd = -1， 每次调用前先判断是否为-1，***************/
    if (-1 != m_sockfd) {
        removefd(m_epollfd, m_sockfd);
        std::cout << "close " << m_sockfd << std::endl;
        m_sockfd = -1;
        --m_user_count;
    }
}

void Http_conn::removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL); //删除epoll上事件，event位置被忽略，传入null就好
    close(fd); // 将套接字关闭
}

void Http_conn::modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int Http_conn::setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK; // 将O_NONBLOCK对应的位加到old_option上
    fcntl(fd, F_SETFL, new_option);
}

void Http_conn::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    // 定义一个epoll事件
    epoll_event event;
    // 将事件的文件描述符设置为 fd，也就是为了fd产生一个epoll事件
    event.data.fd = fd;
    if (1 == TRIGMode) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // 知道增加了第二项ET模式就好了，不用管其他的
    }
    else {
        event.events = EPOLLIN | EPOLLRDHUP; // 知道没有ET模式就好了，不用管其他的
    }
    // 将事件添加
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    /*所有都设置成为非阻塞？*****************************/
    setnonblocking(fd);
}

void Http_conn::process() {
    HTTP_CODE read_ret = process_read();
    /*返回值为NO_REQUEST时候，表示请求不完整，需要继续请求，
    因此加入epoll写事件,下次请求来了再次触发，接着读取*/
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret);
    // 如果写失败，关闭连接，将连接改为epoll写事件，等待下一次请求触发写事件
    // 客户端的请求到服务端会触发读和写请求，
    // 参考：https://cloud.tencent.com/developer/article/1481046
    if (write_ret == false) close_conn();
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}

// m_start_line 记录的是当前已经解析过格式的行的行首，
char* Http_conn::get_line() {
    return m_read_buf + m_start_line;
}

// http报文中每行都是以\r\n结尾,如果单独出现就是错误报文
Http_conn::LINE_STATUS Http_conn::parse_line() {
    char temp;
    for ( ;m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = *(m_read_buf + m_checked_idx);
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx] = '\0';
                m_read_buf[m_checked_idx + 1] = '\0';
                return LINE_OK;
            } 
            // \r只会与\n组合使用作为http的行结尾，其他情况下出现\r语法错误
            return LINE_BAD;
        }
        if (temp == '\n') {
            // 一般是上次读取到\r就到了buffer末尾，没有接收完整，再次接收时会出现这种情况
            if (m_checked_idx > 0 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx] == 0;
                m_read_buf[m_checked_idx - 1] == 0;
                return LINE_OK;
            }
            return LINE_BAD;
        }
        // 什么都没有就继续接受
        return LINE_OPEN;
    }
}


Http_conn::HTTP_CODE Http_conn:: process_read(){
    LINE_STATUS line_status = LINE_OK; // 初始值设置为LINE_OK
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    /*注意 || 前面的line_status == LINE_OK 是为了当解析完content，然后修改line_status 为 LINE_OPEN，进而跳出循环*/
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) 
            || (line_status = parse_line()) == LINE_OK) {
                text = get_line();
                // 将m_start_line更新为刚读取的parse_line结尾的字节，正好是下一行的额开始
                m_start_line = m_checked_idx;
                switch(m_check_state) {
                    case CHECK_STATE_REQUESTLINE:
                        ret = parse_request_line(text);
                        if (BAD_REQUEST == ret) return BAD_REQUEST;
                        break;
                    case CHECK_STATE_HEADER:
                        ret = parse_headers(text);
                        if (BAD_REQUEST == ret) return BAD_REQUEST;
                        // 若是get请求，就直接处理
                        else if (GET_REQUEST == ret) {
                            return do_request();
                        }
                        break;
                    case CHECK_STATE_CONTENT:
                        ret = parse_content(text);
                        // parse_content()不会返回BAD_REQUEST,所以不用判断
                        // if (BAD_REQUEST == ret) return BAD_REQUEST;
                        // 如果解析内容得到GET_REQUEST,就去处理
                        if (GET_REQUEST == ret) {
                            return do_request();
                        }
                        // 跳出循环
                        m_check_state = LINE_OPEN;
                        break;
                    default:
                        return INTERNAL_ERROR;

                } 
    }
}

Http_conn::HTTP_CODE Http_conn::parse_request_line(char *text)
{
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析http请求的一个头部信息
Http_conn::HTTP_CODE Http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
Http_conn::HTTP_CODE Http_conn::parse_content(char *text)
{
    // 如果读取的长度 >= 内容的长度，代表已经全部读取进来了，这个时候用m_string存储数据
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 读取一次
bool Http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE) return false;
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);// 最后一个参数没有特殊需求就设置为0；
        if (bytes_read == -1 ) {
            // 对于非阻塞io，条件成立表示读取完成，跳出；否则继续读取
            // 条件在Linux上为EAGAIN，windows上是EWOULDBLOCK
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            // 否则就返回false；
            else return false;
        }
        /*没有为0的判断***************************/
        m_read_idx += bytes_read;
    }
    return true;
}
Http_conn::HTTP_CODE Http_conn::do_request()
{
    // 将当前根目录位置复制到m_real_file
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // p指向/,根据后面的字符来分情况处理
    const char *p = strrchr(m_url, '/');

    //处理cgi ，就是对数据库中存储的用户信息进行验证
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 不验证的注册
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 不验证的注册
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    // 获取文件权限
    // 没有找到文件，即资源不存在
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    // 不可读，返回FORBIDDEN_REQUEST
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    // 是目录返回请求错误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

bool Http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    // 从可变参数列表写入m_write_buf
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    // 长度超出，就报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}
bool Http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool Http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
bool Http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool Http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool Http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool Http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool Http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}
bool Http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

bool Http_conn::write()
{
    int temp = 0;
    // 添加这一行，用来更新bytes_to_send
    bytes_to_send = m_write_idx;
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }
    /*再看一个这个地方***********************/
    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
/************************函数没有写*/
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }

}

void Http_conn::unmap() {
    if (!m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = nullptr;
    }
}

void Http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}