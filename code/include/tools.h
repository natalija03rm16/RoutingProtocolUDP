int get_port(char* addr)
{
    int ret_port = 5000;
    
    for (int i = 4; i < 7; i++)
    	ret_port += addr[i] - '0';
    	
    return ret_port;
}

//prebaciti u neki header, mozda spojiti sa get_ip
void get_srv_ip(char* ip_addr, char* ip)
{
    int len = strlen(ip_addr);
    
    for (; !((ip_addr[len] >= '0') && (ip_addr[len] <= '9')) ; len--);
    
    int poz = 2;
    for (int i = len; ip_addr[i] != '.'; i--)
    {
    	ip[poz] = ip_addr[i];
    	poz--;
    }	  
}

void make_dest(char* str, TP_Packet* pack)
{
    char pom[5] = ".001";
    for (int i = 0; i < 3; i++)
    	str[i] = pack->dest[i];
    for (int i = 0; i < 5; i++)
    	str[i+3] = pom[i];
    str[8] = '\0';
}
