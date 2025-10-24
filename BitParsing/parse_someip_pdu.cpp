#include <iostream>
#include <string>
#include <vector>
#include <limits>
#include <unordered_map>


struct MessageID
{
    uint16_t service_id;
    uint16_t method_id;
    uint16_t event_id;
    bool is_event_notification{true};
};

struct RequestID
{
    uint16_t client_id;
    uint16_t session_id;
};

enum class MessageType
{
    REQUEST,
    REQUEST_NO_RETURN,
    NOTIFICATION,
    RESPONSE,
    ERROR, 
    INVALID
};

static std::unordered_map<MessageType, std::string> msg_type_to_string = 
{
    {MessageType::REQUEST, "REQUEST"},
    {MessageType::REQUEST_NO_RETURN, "REQUEST_NO_RETURN"},
    {MessageType::NOTIFICATION, "NOTIFICATION"},
    {MessageType::RESPONSE, "RESPONSE"},
    {MessageType::ERROR, "ERROR"}, 
    {MessageType::INVALID, "INVALID"}
};

enum class ReturnCode
{
    OK,
    NOK, 
    NOTAPPLICABLE,
    INVALID
};

static std::unordered_map<ReturnCode, std::string> return_code_to_string = 
{
    {ReturnCode::OK, "OK"},
    {ReturnCode::NOK, "NOK"}, 
    {ReturnCode::NOTAPPLICABLE, "NOTAPPLICABLE"},
    {ReturnCode::INVALID, "INVALID"}
};

struct SOMEIPHeader
{
    MessageID message_id{};
    uint32_t length{};
    RequestID request_id{};
    uint8_t protocol_version{};
    uint8_t iface_version{};
    MessageType msg_type{MessageType::ERROR};
    ReturnCode return_code;

    void print_info()
    {
        //Message ID
        std::cout << "Service ID : 0x" << std::hex << message_id.service_id << std::endl; 
        if (message_id.is_event_notification)
        {
            std::cout << "Event ID : 0x" << std::hex << message_id.event_id << std::endl;
            std::cout << "Message ID indicates an Event Notification" << std::endl;
        }
        else
        {
            std::cout << "Method ID : 0x" << std::hex << message_id.method_id << std::endl;
            std::cout << "Message ID indicates a Method Call (not an event)" << std::endl;
        }
        
        //Length
        std::cout << "Length (payload + rest_of_header) : " << std::dec << length << " bytes" << std::endl;
        
        //Request ID
        std::cout << "Client ID : 0x" << std::hex << request_id.client_id << std::endl;
        std::cout << "Session ID : 0x" << std::hex << request_id.session_id << std::endl;

        //Protocol version
        std::cout << "Protocol version : " << std::dec << +protocol_version << std::endl;

        //Interface Version
        std::cout << "Interface Version : " << std::hex << +iface_version << std::endl;

        //Message type
        std::cout << "Message type : " << msg_type_to_string[msg_type] << std::endl;

        //Return type
        std::cout << "Return code : " << return_code_to_string[return_code] << std::endl;

    }
};

bool is_valid_input (const std::string& someip_pdu)
{
    const size_t num_bytes_in_pdu = someip_pdu.substr(2, std::string::npos).length() / 2;
    const bool has_whole_num_bytes = (num_bytes_in_pdu % 2 == 0);
    const bool has_at_least_header_bytes = (num_bytes_in_pdu >= 16);
    const bool is_valid_hex = true; //todo: implement check

    return has_whole_num_bytes && has_at_least_header_bytes && is_valid_hex;
}

// expects a hex string starting in 0x (for example: 0xAABC..)
// [in]  : hex_str
// [out] : someip_pdu_byte_vector
// [out] : return value (true = pass, false = fail)
bool get_byte_vector_from_hex_string(const std::string& hex_str, std::vector<uint8_t>& someip_pdu_byte_vector)
{
    if (hex_str.substr(0, 2) != "0x")
    {
        std::cerr << "Invalid hex string passed to get_byte_vector_from_hex_string" << std::endl;
        return false;
    }
    
    const std::string stripped_hex_str = hex_str.substr(2, std::string::npos); //remove 0x at the beginning
    for (size_t i = 0; i < stripped_hex_str.length(); i += 2)
    {
        const auto one_byte_hexstring_to_uint8t = [](const std::string& two_hex_characters){return std::stoul("0x" + two_hex_characters, nullptr, 0);};
        someip_pdu_byte_vector.emplace_back(one_byte_hexstring_to_uint8t(stripped_hex_str.substr(i, 2))); // convert two hex characters at a time = 1 byte
    }
    return true;
}

// pick bytes from byte_vector and pack them into an unsigned integer
// [in]  : byte_vector
// [in]  : start and end positions (both included) in byte vector from where bytes need to be picked
// [out] : return value = concatenated_bytes as uint64_t, returns uint64_t::max in case error (all SOME/IP header fields are 32 bits or smaller)
uint64_t concatenate_bytes(const std::vector<uint8_t>& byte_vector, const std::pair<size_t, size_t>& start_and_end_pos)
{
    if (start_and_end_pos.second - start_and_end_pos.first > 3)
    {
        std::cerr << "Error: Cannot concatenate more than 4 bytes into a uint" << std::endl;
        return std::numeric_limits<uint64_t>::max();
    }

    uint64_t concatenated_bytes{0x0};
    uint8_t left_shift = 0;

    for (size_t idx = start_and_end_pos.first; idx <= start_and_end_pos.second; ++idx)
    {
        concatenated_bytes = (concatenated_bytes << left_shift) | static_cast<uint64_t>(byte_vector[idx]);
        left_shift += 8;
    }

    return concatenated_bytes;
}

// [in]  : someip_pdu_byte_vector
// [out] : someip_header
// [out] : return value (true = pass, false = fail)
bool get_someip_header_from_byte_vector(const std::vector<uint8_t>& someip_pdu_byte_vector, SOMEIPHeader& someip_header)
{
    const size_t SOMEIP_HEADERSIZE_IN_BYTES = 16;
    if (someip_pdu_byte_vector.size() >= SOMEIP_HEADERSIZE_IN_BYTES)
    {
        // Message ID
        someip_header.message_id.service_id = static_cast<uint16_t>(concatenate_bytes(someip_pdu_byte_vector, {0, 1})); //todo: remove magic numbers, and handle errors
        const uint16_t bytes_three_and_four = static_cast<uint16_t>(concatenate_bytes(someip_pdu_byte_vector, {2, 3}));
        const bool msb_in_bytes_three_and_four_is_one = (bytes_three_and_four >> 15) == 0x1;
        someip_header.message_id.is_event_notification = msb_in_bytes_three_and_four_is_one;
        const uint16_t lower_fifteen_bits_of_bytes_three_and_four = (bytes_three_and_four << 1) >> 1;
        if (msb_in_bytes_three_and_four_is_one)
            someip_header.message_id.event_id = lower_fifteen_bits_of_bytes_three_and_four;
        else
            someip_header.message_id.method_id = lower_fifteen_bits_of_bytes_three_and_four;
        
        // Length
        someip_header.length = static_cast<uint16_t>(concatenate_bytes(someip_pdu_byte_vector, {4, 7}));
        const size_t num_bytes_after_length = someip_pdu_byte_vector.size() - 8;
        if (num_bytes_after_length != someip_header.length)
        {
            std::cerr << "Error: SOME/IP packet is shorter than expected" << std::endl;
            return false;
        } 

        // Request ID
        someip_header.request_id.client_id = static_cast<uint16_t>(concatenate_bytes(someip_pdu_byte_vector, {8, 9}));
        someip_header.request_id.session_id = static_cast<uint16_t>(concatenate_bytes(someip_pdu_byte_vector, {10, 11}));

        // Protocol version
        someip_header.protocol_version = someip_pdu_byte_vector[12];

        // Interface Version
        someip_header.iface_version = someip_pdu_byte_vector[13];

        // Message Type
        std::unordered_map<uint8_t, MessageType> byte_to_msg_type
        {
            {0x00, MessageType::REQUEST},
            {0x01, MessageType::REQUEST_NO_RETURN},
            {0x02, MessageType::NOTIFICATION},
            {0x80, MessageType::RESPONSE}, 
            {0x81, MessageType::ERROR}
        }; 
        try //todo: if needed, use switch case for no-throw impl. 
        {
            someip_header.msg_type = byte_to_msg_type[someip_pdu_byte_vector[14]];
        }
        catch(const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            someip_header.msg_type = MessageType::INVALID;
            return false;
        }
        
        // Return code
        const uint8_t return_code_byte = someip_pdu_byte_vector[15];
        if (someip_header.msg_type == MessageType::RESPONSE || someip_header.msg_type == MessageType::ERROR)
        {
            if (return_code_byte == 0x00)
                someip_header.return_code = ReturnCode::OK;
            else
                someip_header.return_code = ReturnCode::NOK;
        }
        else
        {
            if (return_code_byte == 0x00)
                someip_header.return_code = ReturnCode::NOTAPPLICABLE;
            else
                someip_header.return_code = ReturnCode::INVALID;
        }

    }
    else
    {
        std::cerr << "SOME/IP PDU is malformed, has less bytes than expected header length" << std::endl;
        return false;

    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Error! Usage : " << argv[0] << " <pdu_hex> \n";
        return EXIT_FAILURE;
    }

    std::string someip_pdu_hex_str{argv[1]}; //user input as a hex (ex: 0x123400560000000C1111222201020000DEADBEEF)
    
    if (!is_valid_input(someip_pdu_hex_str))
    {
        std::cerr << "Invalid SOME/IP packet (not whole number of bytes or improper values)" << std::endl;
        return EXIT_FAILURE;
    }    
    else
    {
        std::cout << "Input is valid SOME/IP PDU, decoding the PDU" << std::endl;
        
        // get a byte vector out of the hex string
        const size_t num_bytes_in_pdu = someip_pdu_hex_str.substr(2, std::string::npos).length() / 2;
        std::vector<uint8_t> someip_pdu_byte_vector;
        someip_pdu_byte_vector.reserve(num_bytes_in_pdu);
        
        if (true == get_byte_vector_from_hex_string(someip_pdu_hex_str, someip_pdu_byte_vector))
        {
            // decode the byte vector
            SOMEIPHeader header;
            if (true == get_someip_header_from_byte_vector(someip_pdu_byte_vector, header))
            {
                // print the header
                header.print_info();

                // print the payload
                std::cout << "Payload : 0x ";
                for (size_t i = 16; i < someip_pdu_byte_vector.size() ; ++i)
                    std::cout << std::hex << +someip_pdu_byte_vector[i] << " ";
                std::cout << std::endl;
            }
            else
            {
                std::cerr << "Error: Invalid SOME/IP Header" << std::endl;
                return EXIT_FAILURE; 
            }

        }
        else
        {
            return EXIT_FAILURE;
        }
        
    }

    return EXIT_SUCCESS;
}
