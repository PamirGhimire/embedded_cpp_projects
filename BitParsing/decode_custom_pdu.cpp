#include <iostream>

constexpr uint32_t PACKET_TYPE_BIT_MASK{0xF0000000};
constexpr uint8_t PACKET_TYPE_RIGHT_SHIFT{28};
constexpr uint32_t SRC_ECU_BIT_MASK{0x0FF00000};
constexpr uint8_t SRC_ECU_RIGHT_SHIFT{20};
constexpr uint32_t DEST_ECU_BIT_MASK{0x000FF000};
constexpr uint8_t DEST_ECU_RIGHT_SHIFT{12};
constexpr uint32_t PDU_FLAGS_BIT_MASK{0x00000F00};
constexpr uint8_t PDU_FLAGS_RIGHT_SHIFT{8};
constexpr uint32_t PAYLOAD_DATA_BIT_MASK{0x000000FF};
constexpr uint8_t PAYLOAD_DATA_RIGHT_SHIFT{0};

enum class PacketType
{
    Heartbeat,
    SensorDataTelemetry,
    ControlCommand,
    DiagnosticMessage,
    Reserved
};


struct Flags
{
    bool high_prio{false};
    bool ack_required{false};
    bool error_flag{false};
    bool reserved{false};
};

struct PDU
{
    PacketType packet_type;
    uint8_t src_id;
    uint8_t dest_id;
    Flags flags;
    uint8_t payload_data;
};

PacketType get_packet_type(const uint32_t& pdu, bool debug_print = false)
{
    const uint8_t packet_type_bits = ((PACKET_TYPE_BIT_MASK & pdu) >> PACKET_TYPE_RIGHT_SHIFT);
    const auto print_if_debug = [debug_print](const std::string& s){if (debug_print) std::cout << s << std::endl;};
    switch (packet_type_bits)
    {
        case 0x0:
            print_if_debug("Packet Type = Heartbeat");
            return PacketType::Heartbeat;
        case 0x1:
            print_if_debug("Packet Type = Sensor Data Telemetry"); 
            return PacketType::SensorDataTelemetry;
        case 0x2:
            print_if_debug("Packet Type = Control Command");
            return PacketType::ControlCommand;
        case 0x3:
            print_if_debug("Packet Type = Diagnostic Message");
            return PacketType::DiagnosticMessage;
        default:
            print_if_debug("Packet Type = Reserved (or other)");
            return PacketType::Reserved;
    }
    return PacketType::Reserved;
}

uint8_t get_src_ecu_id(const uint32_t& pdu, bool debug_print = false)
{
    const uint8_t src_ecu_id = static_cast<uint8_t>((pdu & SRC_ECU_BIT_MASK) >> SRC_ECU_RIGHT_SHIFT);
    if (debug_print)
        std::cout << "Src ECU ID = " << +src_ecu_id << std::endl;
    return src_ecu_id;
}

uint8_t get_dest_ecu_id(const uint32_t& pdu, bool debug_print = false)
{
    const uint8_t dest_ecu_id = static_cast<uint8_t>((pdu & DEST_ECU_BIT_MASK) >> DEST_ECU_RIGHT_SHIFT);
    if (debug_print)
        std::cout << "Dest ECU ID = " << +dest_ecu_id << std::endl;
    return dest_ecu_id;
}

Flags get_pdu_flags(const uint32_t& pdu, bool debug_print = false)
{
    uint8_t four_bits_for_flags = static_cast<uint8_t>((pdu & PDU_FLAGS_BIT_MASK) >> PDU_FLAGS_RIGHT_SHIFT);
    Flags pdu_flags;
    //priority
    pdu_flags.high_prio = ((1 << 3) & four_bits_for_flags) > 0;
    pdu_flags.ack_required = ((1 << 2) & four_bits_for_flags) > 0;
    pdu_flags.error_flag = ((1 << 1) & four_bits_for_flags) > 0;

    if (debug_print)
    {
        if (pdu_flags.high_prio)
            std::cout << "High-Priority : true" << std::endl;
        else
            std::cout << "High-Priority : false" << std::endl;

        if (pdu_flags.ack_required)
            std::cout << "ACK required : true" << std::endl;
        else
            std::cout << "ACK required : false" << std::endl;
        
        if (pdu_flags.error_flag)
            std::cout << "Error flag: true" << std::endl;
        else
            std::cout << "Error flag: false" << std::endl;
    }

    return pdu_flags; // return by value assuming copy elision
}

uint8_t get_pdu_payload_data(const uint32_t& pdu, bool debug_print = false)
{
    uint8_t pdu_payload_data = static_cast<uint8_t>((PAYLOAD_DATA_BIT_MASK & pdu) >> PAYLOAD_DATA_RIGHT_SHIFT); 
    if (debug_print)
        std::cout << "Payload: " << +pdu_payload_data << std::endl;
    return pdu_payload_data;
}

PDU decode(const uint32_t pdu, bool debug_print = false)
{
    PDU decoded_pdu;
    decoded_pdu.packet_type = get_packet_type(pdu, debug_print);
    decoded_pdu.src_id = get_src_ecu_id(pdu, debug_print);
    decoded_pdu.dest_id = get_dest_ecu_id(pdu, debug_print);
    decoded_pdu.flags = get_pdu_flags(pdu, debug_print);
    decoded_pdu.payload_data = get_pdu_payload_data(pdu, debug_print);
    return decoded_pdu;
}


int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage : " << argv[0] << " <uint32_value>\n";
        return 1;
    }

    const uint32_t pdu = static_cast<uint32_t>(std::stoul(argv[1], nullptr, 0));
    const bool debug_print = true;
    PDU decoded_pdu = decode(pdu, debug_print);

    return 0;
}
