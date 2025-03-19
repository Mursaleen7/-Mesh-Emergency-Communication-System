/**
 * Resilient Mesh Emergency Communication System
 * Enhanced with GPS, Offline Maps, and RF Triangulation
 * For Emergency Response Operations
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>  /* For sockaddr_in and INADDR_ANY */
#include <arpa/inet.h>   /* For inet_addr and inet_ntoa */
#include <math.h>        /* For triangulation calculations */

/* ---- Configuration Constants ---- */
#define MAX_NODES 64
#define MAX_ROUTES 16
#define MAX_MSG_SIZE 1024  // Increased to support map data
#define MAX_QUEUE_SIZE 32
#define NODE_TIMEOUT_MS 300000   // 5 minutes
#define BEACON_INTERVAL_MS 10000 // 10 seconds
#define LOCATION_UPDATE_INTERVAL_MS 15000 // 15 seconds
#define RETRY_INTERVAL_MS 5000   // 5 seconds
#define MAX_HOPS 8
#define BATTERY_CRIT_THRESHOLD 10
#define BATTERY_LOW_THRESHOLD 25
#define ENCRYPTION_KEY_SIZE 32
#define MAC_SIZE 16
#define RF_CHANNEL_COUNT 16
#define DEBUG 1 // Enable debug output
#define MAX_MAP_TILES 16
#define MAX_POI 32     // Points of Interest
#define MAX_ZONES 8    // Rescue zones
#define SHARED_KEY "EMERGENCY_MESH_NETWORK_KEY_2025" // Use a fixed key for all nodes

/* ---- System Typedefs ---- */
typedef uint16_t node_id_t;
typedef uint8_t hop_count_t;
typedef uint8_t msg_id_t;
typedef uint8_t battery_level_t;
typedef uint8_t signal_strength_t;

/* ---- Message Types ---- */
typedef enum {
    MSG_BEACON = 0x01,
    MSG_DATA = 0x02,
    MSG_ACK = 0x03,
    MSG_ROUTE_REQUEST = 0x04,
    MSG_ROUTE_RESPONSE = 0x05,
    MSG_ALERT = 0x06,
    MSG_PING = 0x07,
    MSG_PONG = 0x08,
    MSG_LOCATION = 0x09,      // New message type for location updates
    MSG_MAP_REQUEST = 0x0A,   // Request map tile
    MSG_MAP_DATA = 0x0B,      // Send map tile data
    MSG_POI = 0x0C,           // Point of Interest
    MSG_ZONE = 0x0D           // Rescue zone
} message_type_t;

/* ---- Message Priority Levels ---- */
typedef enum {
    PRIORITY_LOW = 0,
    PRIORITY_NORMAL = 1,
    PRIORITY_HIGH = 2,
    PRIORITY_EMERGENCY = 3
} message_priority_t;

/* ---- GPS Fix Quality ---- */
typedef enum {
    GPS_FIX_NONE = 0,
    GPS_FIX_GPS = 1,
    GPS_FIX_DGPS = 2,
    GPS_FIX_ESTIMATED = 3,    // RF triangulation
    GPS_FIX_MANUAL = 4,       // Manually entered
    GPS_FIX_SIMULATION = 5    // Simulated position
} gps_fix_t;

/* ---- Enhanced Geographic Location ---- */
typedef struct {
    double latitude;           // Decimal degrees
    double longitude;          // Decimal degrees
    float altitude;            // Meters above sea level
    float speed;               // Meters per second
    float course;              // Degrees from true north
    float hdop;                // Horizontal dilution of precision
    uint8_t satellites;        // Number of satellites in use
    uint32_t timestamp;        // Time of fix
    gps_fix_t fix_quality;     // Quality of the GPS fix
} geo_location_t;

/* ---- Point of Interest ---- */
typedef struct {
    geo_location_t location;
    char name[32];
    char description[128];
    uint8_t type;             // 0=general, 1=shelter, 2=medical, 3=water, 4=food
    uint32_t timestamp;
} poi_t;

/* ---- Rescue Zone ---- */
typedef struct {
    geo_location_t center;
    float radius;             // Meters
    char name[32];
    char description[128];
    uint8_t status;           // 0=proposed, 1=active, 2=clearing, 3=cleared
    uint32_t timestamp;
} rescue_zone_t;

/* ---- Map Tile ---- */
typedef struct {
    uint16_t tile_id;         // X/Y grid identifier
    uint16_t zoom_level;      // Zoom level
    uint32_t data_size;       // Size of map data
    uint32_t timestamp;       // Last update time
    uint8_t* data;            // Compressed map data
} map_tile_t;

/* ---- Message Structure ---- */
typedef struct {
    message_type_t type;
    node_id_t source;
    node_id_t destination;
    msg_id_t id;
    hop_count_t hop_count;
    uint8_t ttl;
    message_priority_t priority;
    uint32_t timestamp;
    uint16_t payload_len;
    uint8_t payload[MAX_MSG_SIZE];
    uint8_t mac[MAC_SIZE];  // Message Authentication Code
} message_t;

/* ---- Node Information ---- */
typedef struct {
    node_id_t id;
    uint32_t last_seen;
    signal_strength_t signal_strength;
    battery_level_t battery_level;
    hop_count_t hop_count;
    geo_location_t location;
    bool is_relay;
    uint8_t rf_channel;
    
    // RF triangulation data
    uint32_t last_ping_time;
    uint32_t rtt;             // Round trip time in ms
    float distance_estimate;  // Estimated distance in meters
} node_info_t;

/* ---- Routing Table Entry ---- */
typedef struct {
    node_id_t destination;
    node_id_t next_hop;
    hop_count_t hop_count;
    uint32_t last_updated;
    signal_strength_t signal_quality;
} route_entry_t;

/* ---- Message Queue Entry ---- */
typedef struct msg_queue_entry {
    message_t message;
    uint32_t next_retry;
    uint8_t retry_count;
    bool awaiting_ack;
    struct msg_queue_entry* next;
} msg_queue_entry_t;

/* ---- Hardware Abstraction Layer (HAL) ---- */
/**
 * Hardware abstraction layer to isolate platform-specific code
 * Implementations would vary based on the target microcontroller
 */
typedef struct {
    void (*init_radio)(void);
    bool (*send_packet)(uint8_t* data, uint16_t len, uint8_t power_level);
    uint16_t (*receive_packet)(uint8_t* buffer, uint16_t max_len);
    void (*sleep_ms)(uint32_t ms);
    uint32_t (*get_time_ms)(void);
    battery_level_t (*get_battery_level)(void);
    void (*set_led)(bool state);
    bool (*get_button_state)(uint8_t button_id);
    void (*enter_low_power_mode)(void);
    void (*exit_low_power_mode)(void);
    void (*set_rf_channel)(uint8_t channel);
    signal_strength_t (*get_last_rssi)(void);
    int16_t (*get_temperature)(void);
    uint8_t (*get_random_byte)(void);
    bool (*get_gps_data)(geo_location_t* location);
    
    // Additional fields for macOS implementation
    uint8_t* receiver_buffer;
    uint16_t receiver_len;
} hal_t;

/* ---- Map and Location System State ---- */
typedef struct {
    map_tile_t map_tiles[MAX_MAP_TILES];
    uint8_t map_tile_count;
    
    poi_t points_of_interest[MAX_POI];
    uint8_t poi_count;
    
    rescue_zone_t rescue_zones[MAX_ZONES];
    uint8_t zone_count;
    
    geo_location_t last_known_locations[MAX_NODES];
    uint32_t location_timestamps[MAX_NODES];
    
    uint32_t last_location_broadcast;
} map_system_t;

/* ---- System State ---- */
typedef struct {
    hal_t hal;
    node_id_t node_id;
    uint8_t encryption_key[ENCRYPTION_KEY_SIZE];
    geo_location_t location;
    node_info_t known_nodes[MAX_NODES];
    uint8_t node_count;
    route_entry_t routing_table[MAX_ROUTES];
    uint8_t route_count;
    msg_queue_entry_t* outbound_queue;
    uint8_t outbound_queue_size;
    uint32_t last_beacon_time;
    battery_level_t battery_level;
    uint8_t current_rf_channel;
    bool low_power_mode;
    msg_id_t next_msg_id;
    
    // Map and location system
    map_system_t map_system;
} system_state_t;

/* ---- Function Prototypes ---- */

// Initialization
void system_init(system_state_t* state, hal_t hal, node_id_t node_id);
void set_encryption_key(system_state_t* state, const uint8_t* key);
void set_location(system_state_t* state, geo_location_t location);

// Network Management
void process_incoming_messages(system_state_t* state);
void send_beacon(system_state_t* state);
void update_node_info(system_state_t* state, node_id_t id, signal_strength_t signal, 
                     battery_level_t battery, hop_count_t hops, geo_location_t* location);
node_info_t* find_node(system_state_t* state, node_id_t id);
void clean_stale_nodes(system_state_t* state);

// Routing
route_entry_t* find_route(system_state_t* state, node_id_t destination);
bool add_or_update_route(system_state_t* state, node_id_t destination, 
                         node_id_t next_hop, hop_count_t hop_count, signal_strength_t quality);
void send_route_request(system_state_t* state, node_id_t destination);
void process_route_request(system_state_t* state, message_t* msg);
void process_route_response(system_state_t* state, message_t* msg);
void clean_stale_routes(system_state_t* state);

// Message Handling
bool send_message(system_state_t* state, node_id_t destination, 
                 message_type_t type, const uint8_t* payload, 
                 uint16_t payload_len, message_priority_t priority);
void forward_message(system_state_t* state, message_t* msg);
void process_message(system_state_t* state, message_t* msg);
void enqueue_message(system_state_t* state, message_t* msg);
void process_queue(system_state_t* state);
void acknowledge_message(system_state_t* state, message_t* msg);
void remove_from_queue(system_state_t* state, msg_id_t id, node_id_t destination);

// Encryption & Security
void encrypt_message(system_state_t* state, message_t* msg);
bool decrypt_message(system_state_t* state, message_t* msg);
void calculate_mac(system_state_t* state, message_t* msg);
bool verify_mac(system_state_t* state, message_t* msg);

// Power Management
void check_battery_status(system_state_t* state);
void adapt_power_settings(system_state_t* state);
void enter_low_power_mode(system_state_t* state);
void exit_low_power_mode(system_state_t* state);

// Channel Management
void scan_channels(system_state_t* state);
void switch_channel(system_state_t* state, uint8_t channel);

// Bluetooth specific functions
void bt_scan_for_devices(void);
hal_t create_bt_hal(void);
const char* get_message_type_name(message_type_t type);

// GPS and Location functions
void update_gps_location(system_state_t* state);
void broadcast_location(system_state_t* state);
void process_location_message(system_state_t* state, message_t* msg);
geo_location_t* get_node_location(system_state_t* state, node_id_t node_id);
void update_node_location(system_state_t* state, node_id_t node_id, geo_location_t* location);
char* format_location_string(geo_location_t* location, char* buffer, size_t buffer_size);

// RF Triangulation
void estimate_node_distance(system_state_t* state, node_id_t node_id);
void triangulate_position(system_state_t* state, node_id_t node_id);
float calculate_distance_from_rssi(signal_strength_t rssi);
float calculate_distance_from_rtt(uint32_t rtt);

// Map System
void init_map_system(map_system_t* map_system);
bool load_map_tile(map_system_t* map_system, uint16_t tile_id, uint16_t zoom_level);
void request_map_tile(system_state_t* state, uint16_t tile_id, uint16_t zoom_level);
void process_map_request(system_state_t* state, message_t* msg);
void process_map_data(system_state_t* state, message_t* msg);
void add_point_of_interest(system_state_t* state, poi_t* poi);
void broadcast_point_of_interest(system_state_t* state, poi_t* poi);
void process_poi_message(system_state_t* state, message_t* msg);
void add_rescue_zone(system_state_t* state, rescue_zone_t* zone);
void broadcast_rescue_zone(system_state_t* state, rescue_zone_t* zone);
void process_zone_message(system_state_t* state, message_t* msg);
bool is_in_rescue_zone(geo_location_t* location, rescue_zone_t* zone);

/* ---- Bluetooth specific data structures ---- */
#define BT_MAX_DEVICES 10

typedef struct {
    char addr[18];          // Bluetooth address as string (e.g., "00:11:22:33:44:55")
    char name[248];         // Device name
    int8_t rssi;            // Signal strength
    uint8_t connected;      // Connection status
} bt_device_t;

// Bluetooth state
static int bt_socket = -1;
static bt_device_t known_bt_devices[BT_MAX_DEVICES];
static int bt_device_count = 0;
static uint8_t bt_rx_buffer[MAX_MSG_SIZE];
static uint16_t bt_rx_buffer_len = 0;
static uint8_t bt_last_rssi = 0;
static int bt_port = 31415;  // Port used for Bluetooth communication

/* ---- Helper Functions ---- */

/**
 * Get the name of a message type for debugging
 */
const char* get_message_type_name(message_type_t type) {
    switch (type) {
        case MSG_BEACON: return "BEACON";
        case MSG_DATA: return "DATA";
        case MSG_ACK: return "ACK";
        case MSG_ROUTE_REQUEST: return "ROUTE_REQUEST";
        case MSG_ROUTE_RESPONSE: return "ROUTE_RESPONSE";
        case MSG_ALERT: return "ALERT";
        case MSG_PING: return "PING";
        case MSG_PONG: return "PONG";
        case MSG_LOCATION: return "LOCATION";
        case MSG_MAP_REQUEST: return "MAP_REQUEST";
        case MSG_MAP_DATA: return "MAP_DATA";
        case MSG_POI: return "POI";
        case MSG_ZONE: return "ZONE";
        default: return "UNKNOWN";
    }
}

/**
 * Calculate the Haversine distance between two geographic points (in meters)
 */
float haversine_distance(double lat1, double lon1, double lat2, double lon2) {
    // Convert latitude and longitude from degrees to radians
    lat1 *= M_PI / 180.0;
    lon1 *= M_PI / 180.0;
    lat2 *= M_PI / 180.0;
    lon2 *= M_PI / 180.0;
    
    // Haversine formula
    double dlon = lon2 - lon1;
    double dlat = lat2 - lat1;
    double a = pow(sin(dlat / 2), 2) + cos(lat1) * cos(lat2) * pow(sin(dlon / 2), 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    double r = 6371000; // Earth radius in meters
    
    return (float)(r * c);
}

/**
 * Format a geo location into a human-readable string
 */
char* format_location_string(geo_location_t* location, char* buffer, size_t buffer_size) {
    const char* fix_types[] = {"NO FIX", "GPS", "DGPS", "ESTIMATED", "MANUAL", "SIMULATION"};
    const char* fix_type = (location->fix_quality <= GPS_FIX_SIMULATION) ? 
                          fix_types[location->fix_quality] : "UNKNOWN";
    
    // Convert decimal degrees to degrees, minutes, seconds
    double lat_abs = fabs(location->latitude);
    int lat_deg = (int)lat_abs;
    double lat_min = (lat_abs - lat_deg) * 60.0;
    
    double lon_abs = fabs(location->longitude);
    int lon_deg = (int)lon_abs;
    double lon_min = (lon_abs - lon_deg) * 60.0;
    
    snprintf(buffer, buffer_size, 
            "%d°%.4f'%c, %d°%.4f'%c, Alt:%.1fm, Fix:%s, Sats:%d", 
            lat_deg, lat_min, (location->latitude >= 0) ? 'N' : 'S',
            lon_deg, lon_min, (location->longitude >= 0) ? 'E' : 'W',
            location->altitude, fix_type, location->satellites);
    
    return buffer;
}

/* ---- GPS and Location Functions ---- */

/**
 * Update the local GPS position
 */
void update_gps_location(system_state_t* state) {
    geo_location_t new_location;
    
    // Try to get GPS data from hardware
    if (state->hal.get_gps_data(&new_location)) {
        // Update our local position
        set_location(state, new_location);
        
        // Store in our location history as well
        update_node_location(state, state->node_id, &new_location);
        
        if (DEBUG) {
            char loc_str[128];
            format_location_string(&new_location, loc_str, sizeof(loc_str));
            printf("Updated GPS position: %s\n", loc_str);
        }
    } else if (DEBUG) {
        printf("Failed to get GPS fix\n");
    }
}

/**
 * Broadcast our location to the network
 */
void broadcast_location(system_state_t* state) {
    uint32_t current_time = state->hal.get_time_ms();
    
    // Only broadcast at configured intervals and if we have valid data
    if (current_time - state->map_system.last_location_broadcast >= LOCATION_UPDATE_INTERVAL_MS &&
        state->location.fix_quality != GPS_FIX_NONE) {
        
        if (DEBUG) printf("Broadcasting location...\n");
        
        // Create a GPS update message
        uint8_t payload[sizeof(geo_location_t)];
        memcpy(payload, &state->location, sizeof(geo_location_t));
        
        // Send as broadcast with normal priority
        send_message(state, 0xFFFF, MSG_LOCATION, payload, sizeof(geo_location_t), PRIORITY_NORMAL);
        
        state->map_system.last_location_broadcast = current_time;
    }
}

/**
 * Process a received location update message
 */
void process_location_message(system_state_t* state, message_t* msg) {
    if (msg->payload_len >= sizeof(geo_location_t)) {
        geo_location_t location;
        memcpy(&location, msg->payload, sizeof(geo_location_t));
        
        // Update the node's location in our database
        update_node_location(state, msg->source, &location);
        
        if (DEBUG) {
            char loc_str[128];
            format_location_string(&location, loc_str, sizeof(loc_str));
            printf("Updated location for node %d: %s\n", msg->source, loc_str);
        }
    }
}

/**
 * Get the location of a specific node
 */
geo_location_t* get_node_location(system_state_t* state, node_id_t node_id) {
    // If it's our own ID, return our current location
    if (node_id == state->node_id) {
        return &state->location;
    }
    
    // Check our location database
    for (int i = 0; i < MAX_NODES; i++) {
        if (state->map_system.location_timestamps[i] > 0 && 
            i < state->node_count && 
            state->known_nodes[i].id == node_id) {
            return &state->map_system.last_known_locations[i];
        }
    }
    
    return NULL;
}

/**
 * Update a node's location in our database
 */
void update_node_location(system_state_t* state, node_id_t node_id, geo_location_t* location) {
    // If it's our own node, update the main location
    if (node_id == state->node_id) {
        state->location = *location;
    }
    
    // Find the node's entry or create a new one
    for (int i = 0; i < state->node_count; i++) {
        if (state->known_nodes[i].id == node_id) {
            // Update existing entry
            state->map_system.last_known_locations[i] = *location;
            state->map_system.location_timestamps[i] = state->hal.get_time_ms();
            
            // Also update the location in the node info
            state->known_nodes[i].location = *location;
            return;
        }
    }
    
    // If we reach here, the node isn't known yet - wait for it to be added first
}

/* ---- RF Triangulation Functions ---- */

/**
 * Estimate distance to a node based on signal strength and/or round trip time
 */
void estimate_node_distance(system_state_t* state, node_id_t node_id) {
    node_info_t* node = find_node(state, node_id);
    if (!node) return;
    
    // Method 1: RSSI-based distance estimation
    float rssi_distance = calculate_distance_from_rssi(node->signal_strength);
    
    // Method 2: RTT-based distance estimation (if available)
    float rtt_distance = 0;
    if (node->rtt > 0) {
        rtt_distance = calculate_distance_from_rtt(node->rtt);
    }
    
    // Combine the two estimates (weighted average)
    if (node->rtt > 0) {
        node->distance_estimate = (rssi_distance * 0.7f) + (rtt_distance * 0.3f);
    } else {
        node->distance_estimate = rssi_distance;
    }
    
    if (DEBUG) {
        printf("Estimated distance to node %d: %.2f meters (RSSI: %.2f m, RTT: %.2f m)\n", 
              node_id, node->distance_estimate, rssi_distance, rtt_distance);
    }
}

/**
 * Calculate approximate distance based on RSSI value
 * Uses a simplified log-distance path loss model
 */
float calculate_distance_from_rssi(signal_strength_t rssi) {
    // RSSI at 1 meter distance (calibration value)
    const float RSSI_1M = -40.0f;  
    
    // Path loss exponent (typically 2.0 to 4.0 depending on environment)
    const float PATH_LOSS_EXPONENT = 2.5f;
    
    // Convert RSSI to dBm (assuming RSSI is 0-255 range)
    float rssi_dbm = ((float)rssi / 255.0f) * (-30.0f) - 70.0f;
    
    // Calculate distance using log-distance path loss model
    float distance = pow(10.0f, (RSSI_1M - rssi_dbm) / (10.0f * PATH_LOSS_EXPONENT));
    
    // Constrain result to reasonable values
    if (distance < 0.1f) distance = 0.1f;
    if (distance > 1000.0f) distance = 1000.0f;
    
    return distance;
}

/**
 * Calculate approximate distance based on round-trip time
 * This assumes radio waves travel at the speed of light
 */
float calculate_distance_from_rtt(uint32_t rtt) {
    // Speed of light in air: ~299,702,547 m/s
    // Distance = (RTT / 2) * speed of light
    // RTT is in milliseconds, so convert to seconds
    
    // Factor in processing delay (estimated as 1ms)
    uint32_t adjusted_rtt = (rtt > 1) ? rtt - 1 : 0;
    
    float distance = ((float)adjusted_rtt / 2000.0f) * 299702547.0f;
    
    // Constrain result to reasonable values
    if (distance < 0.1f) distance = 0.1f;
    if (distance > 1000.0f) distance = 1000.0f;
    
    return distance;
}

/**
 * Triangulate position of a node based on our position and distances to other nodes
 * Requires at least 3 nodes with known positions and estimated distances
 */
void triangulate_position(system_state_t* state, node_id_t target_node_id) {
    node_info_t* target = find_node(state, target_node_id);
    if (!target) return;
    
    // We need our own position to be valid
    if (state->location.fix_quality == GPS_FIX_NONE) {
        if (DEBUG) printf("Cannot triangulate without own position\n");
        return;
    }
    
    // Count how many nodes we have with valid positions and distance estimates
    int valid_nodes = 0;
    node_info_t* reference_nodes[MAX_NODES];
    
    for (int i = 0; i < state->node_count; i++) {
        node_info_t* node = &state->known_nodes[i];
        
        // Skip the target node and ourselves
        if (node->id == target_node_id || node->id == state->node_id) continue;
        
        // Skip nodes without valid position
        geo_location_t* node_location = get_node_location(state, node->id);
        if (!node_location || node_location->fix_quality == GPS_FIX_NONE) continue;
        
        // Calculate distance estimate if we don't have one
        if (node->distance_estimate <= 0) {
            estimate_node_distance(state, node->id);
        }
        
        // If we have a valid distance estimate, include this node
        if (node->distance_estimate > 0) {
            reference_nodes[valid_nodes++] = node;
            
            if (valid_nodes >= 3) break; // We only need 3 reference points
        }
    }
    
    // Can't triangulate with fewer than 3 reference points
    if (valid_nodes < 3) {
        if (DEBUG) printf("Not enough reference nodes to triangulate position\n");
        return;
    }
    
    // We use a simple centroid algorithm with distance weighting
    // This is a simplified approach - for better accuracy we would use
    // multilateration or least squares optimization
    double lat_sum = 0, lon_sum = 0, weight_sum = 0;
    
    // Our own position is very important, so include it with a high weight
    geo_location_t* own_location = &state->location;
    float own_distance = target->distance_estimate;
    
    lat_sum += own_location->latitude * (1.0 / own_distance);
    lon_sum += own_location->longitude * (1.0 / own_distance);
    weight_sum += (1.0 / own_distance);
    
    // Add the reference nodes
    for (int i = 0; i < valid_nodes; i++) {
        node_info_t* ref_node = reference_nodes[i];
        geo_location_t* ref_location = get_node_location(state, ref_node->id);
        float distance = ref_node->distance_estimate;
        
        if (ref_location) {
            lat_sum += ref_location->latitude * (1.0 / distance);
            lon_sum += ref_location->longitude * (1.0 / distance);
            weight_sum += (1.0 / distance);
        }
    }
    
    // Calculate weighted average
    double estimated_lat = lat_sum / weight_sum;
    double estimated_lon = lon_sum / weight_sum;
    
    // Update the target node's location
    target->location.latitude = estimated_lat;
    target->location.longitude = estimated_lon;
    target->location.fix_quality = GPS_FIX_ESTIMATED;
    target->location.timestamp = state->hal.get_time_ms();
    
    // Also update our location database
    update_node_location(state, target_node_id, &target->location);
    
    if (DEBUG) {
        char loc_str[128];
        format_location_string(&target->location, loc_str, sizeof(loc_str));
        printf("Triangulated position for node %d: %s\n", target_node_id, loc_str);
    }
}

/* ---- Map System Functions ---- */

/**
 * Initialize the map system
 */
void init_map_system(map_system_t* map_system) {
    memset(map_system, 0, sizeof(map_system_t));
    
    // Initialize map tiles
    for (int i = 0; i < MAX_MAP_TILES; i++) {
        map_system->map_tiles[i].data = NULL;
    }
    
    // Reset POI and zones
    map_system->poi_count = 0;
    map_system->zone_count = 0;
    
    // Reset location tracking
    for (int i = 0; i < MAX_NODES; i++) {
        map_system->location_timestamps[i] = 0;
    }
}

/**
 * Load a map tile from storage
 */
bool load_map_tile(map_system_t* map_system, uint16_t tile_id, uint16_t zoom_level) {
    char filename[64];
    snprintf(filename, sizeof(filename), "map_tile_%d_%d.osm", tile_id, zoom_level);
    
    // Check if we already have this tile
    for (int i = 0; i < map_system->map_tile_count; i++) {
        if (map_system->map_tiles[i].tile_id == tile_id && 
            map_system->map_tiles[i].zoom_level == zoom_level) {
            if (DEBUG) printf("Map tile %d (zoom %d) already loaded\n", tile_id, zoom_level);
            return true;
        }
    }
    
    // Try to open the file
    FILE* file = fopen(filename, "rb");
    if (!file) {
        if (DEBUG) printf("Map tile file %s not found\n", filename);
        return false;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Allocate memory
    uint8_t* data = malloc(file_size);
    if (!data) {
        if (DEBUG) printf("Failed to allocate memory for map tile\n");
        fclose(file);
        return false;
    }
    
    // Read data
    if (fread(data, 1, file_size, file) != (size_t)file_size) {
        if (DEBUG) printf("Failed to read map tile data\n");
        free(data);
        fclose(file);
        return false;
    }
    
    fclose(file);
    
    // Add to our collection if we have space
    if (map_system->map_tile_count < MAX_MAP_TILES) {
        map_tile_t* tile = &map_system->map_tiles[map_system->map_tile_count++];
        tile->tile_id = tile_id;
        tile->zoom_level = zoom_level;
        tile->data_size = file_size;
        tile->timestamp = time(NULL);
        tile->data = data;
        
        if (DEBUG) printf("Loaded map tile %d (zoom %d), %ld bytes\n", 
                         tile_id, zoom_level, file_size);
        return true;
    } else {
        if (DEBUG) printf("No space for new map tile\n");
        free(data);
        return false;
    }
}

/**
 * Request a map tile from the network
 */
void request_map_tile(system_state_t* state, uint16_t tile_id, uint16_t zoom_level) {
    if (DEBUG) printf("Requesting map tile %d (zoom %d) from network\n", tile_id, zoom_level);
    
    // Create payload with tile ID and zoom level
    uint8_t payload[4];
    payload[0] = tile_id & 0xFF;
    payload[1] = (tile_id >> 8) & 0xFF;
    payload[2] = zoom_level & 0xFF;
    payload[3] = (zoom_level >> 8) & 0xFF;
    
    // Broadcast request
    send_message(state, 0xFFFF, MSG_MAP_REQUEST, payload, sizeof(payload), PRIORITY_LOW);
}

/**
 * Process a map tile request
 */
void process_map_request(system_state_t* state, message_t* msg) {
    if (msg->payload_len < 4) return;
    
    // Extract tile ID and zoom level
    uint16_t tile_id = msg->payload[0] | (msg->payload[1] << 8);
    uint16_t zoom_level = msg->payload[2] | (msg->payload[3] << 8);
    
    if (DEBUG) printf("Received request for map tile %d (zoom %d)\n", tile_id, zoom_level);
    
    // Check if we have this tile
    for (int i = 0; i < state->map_system.map_tile_count; i++) {
        map_tile_t* tile = &state->map_system.map_tiles[i];
        
        if (tile->tile_id == tile_id && tile->zoom_level == zoom_level && tile->data != NULL) {
            if (DEBUG) printf("Sending requested map tile\n");
            
            // We have the tile, send it back
            // We would normally fragment large tiles, but for simplicity
            // we'll assume they fit in a single message
            if (tile->data_size <= MAX_MSG_SIZE - 8) {
                uint8_t payload[MAX_MSG_SIZE];
                
                // Add header with tile ID, zoom level, and size
                payload[0] = tile_id & 0xFF;
                payload[1] = (tile_id >> 8) & 0xFF;
                payload[2] = zoom_level & 0xFF;
                payload[3] = (zoom_level >> 8) & 0xFF;
                payload[4] = tile->data_size & 0xFF;
                payload[5] = (tile->data_size >> 8) & 0xFF;
                payload[6] = (tile->data_size >> 16) & 0xFF;
                payload[7] = (tile->data_size >> 24) & 0xFF;
                
                // Copy tile data
                memcpy(payload + 8, tile->data, tile->data_size);
                
                // Send to requester
                send_message(state, msg->source, MSG_MAP_DATA, 
                           payload, tile->data_size + 8, PRIORITY_LOW);
            } else {
                // Tile too large for a single message - would require fragmentation
                if (DEBUG) printf("Map tile too large to send (%d bytes)\n", tile->data_size);
            }
            return;
        }
    }
    
    // We don't have this tile
    if (DEBUG) printf("We don't have the requested map tile\n");
}

/**
 * Process received map tile data
 */
void process_map_data(system_state_t* state, message_t* msg) {
    if (msg->payload_len < 8) return;
    
    // Extract header info
    uint16_t tile_id = msg->payload[0] | (msg->payload[1] << 8);
    uint16_t zoom_level = msg->payload[2] | (msg->payload[3] << 8);
    uint32_t data_size = msg->payload[4] | (msg->payload[5] << 8) | 
                        (msg->payload[6] << 16) | (msg->payload[7] << 24);
    
    if (DEBUG) printf("Received map tile %d (zoom %d), %d bytes\n", 
                     tile_id, zoom_level, data_size);
    
    // Verify data size matches what we received
    if (msg->payload_len - 8 != data_size) {
        if (DEBUG) printf("Map data size mismatch\n");
        return;
    }
    
    // Check if we already have this tile
    for (int i = 0; i < state->map_system.map_tile_count; i++) {
        if (state->map_system.map_tiles[i].tile_id == tile_id && 
            state->map_system.map_tiles[i].zoom_level == zoom_level) {
            
            // Free old data
            if (state->map_system.map_tiles[i].data) {
                free(state->map_system.map_tiles[i].data);
            }
            
            // Allocate and copy new data
            uint8_t* data = malloc(data_size);
            if (data) {
                memcpy(data, msg->payload + 8, data_size);
                state->map_system.map_tiles[i].data = data;
                state->map_system.map_tiles[i].data_size = data_size;
                state->map_system.map_tiles[i].timestamp = state->hal.get_time_ms();
                
                if (DEBUG) printf("Updated existing map tile\n");
                return;
            }
        }
    }
    
    // We don't have this tile yet, add it if we have space
    if (state->map_system.map_tile_count < MAX_MAP_TILES) {
        map_tile_t* tile = &state->map_system.map_tiles[state->map_system.map_tile_count];
        
        // Allocate and copy data
        uint8_t* data = malloc(data_size);
        if (data) {
            memcpy(data, msg->payload + 8, data_size);
            
            tile->tile_id = tile_id;
            tile->zoom_level = zoom_level;
            tile->data = data;
            tile->data_size = data_size;
            tile->timestamp = state->hal.get_time_ms();
            
            state->map_system.map_tile_count++;
            
            if (DEBUG) printf("Added new map tile\n");
            
            // Save to file for persistence
            char filename[64];
            snprintf(filename, sizeof(filename), "map_tile_%d_%d.osm", tile_id, zoom_level);
            FILE* file = fopen(filename, "wb");
            if (file) {
                fwrite(data, 1, data_size, file);
                fclose(file);
                if (DEBUG) printf("Saved map tile to %s\n", filename);
            }
        }
    } else {
        if (DEBUG) printf("No space for new map tile\n");
    }
}

/**
 * Add a point of interest to our database
 */
void add_point_of_interest(system_state_t* state, poi_t* poi) {
    // Check if we already have this POI (by name and location)
    for (int i = 0; i < state->map_system.poi_count; i++) {
        poi_t* existing = &state->map_system.points_of_interest[i];
        
        // Compare name
        if (strcmp(existing->name, poi->name) == 0) {
            // Same name, check if the location is close
            float distance = haversine_distance(
                existing->location.latitude, existing->location.longitude,
                poi->location.latitude, poi->location.longitude);
            
            if (distance < 50.0f) {  // Within 50 meters
                // Update the existing POI
                memcpy(existing, poi, sizeof(poi_t));
                if (DEBUG) printf("Updated existing POI: %s\n", poi->name);
                return;
            }
        }
    }
    
    // Add new POI if we have space
    if (state->map_system.poi_count < MAX_POI) {
        memcpy(&state->map_system.points_of_interest[state->map_system.poi_count], 
              poi, sizeof(poi_t));
        state->map_system.poi_count++;
        
        if (DEBUG) printf("Added new POI: %s\n", poi->name);
    } else {
        if (DEBUG) printf("No space for new POI\n");
    }
}

/**
 * Broadcast a point of interest to the network
 */
void broadcast_point_of_interest(system_state_t* state, poi_t* poi) {
    if (DEBUG) printf("Broadcasting POI: %s\n", poi->name);
    
    // Pack the POI data
    uint8_t payload[sizeof(poi_t)];
    memcpy(payload, poi, sizeof(poi_t));
    
    // Broadcast with normal priority
    send_message(state, 0xFFFF, MSG_POI, payload, sizeof(poi_t), PRIORITY_NORMAL);
}

/**
 * Process a received POI message
 */
void process_poi_message(system_state_t* state, message_t* msg) {
    if (msg->payload_len < sizeof(poi_t)) return;
    
    // Extract POI data
    poi_t poi;
    memcpy(&poi, msg->payload, sizeof(poi_t));
    
    if (DEBUG) {
        char loc_str[128];
        format_location_string(&poi.location, loc_str, sizeof(loc_str));
        printf("Received POI: %s at %s\n", poi.name, loc_str);
    }
    
    // Add to our database
    add_point_of_interest(state, &poi);
}

/**
 * Add a rescue zone to our database
 */
void add_rescue_zone(system_state_t* state, rescue_zone_t* zone) {
    // Check if we already have this zone (by name and location)
    for (int i = 0; i < state->map_system.zone_count; i++) {
        rescue_zone_t* existing = &state->map_system.rescue_zones[i];
        
        // Compare name
        if (strcmp(existing->name, zone->name) == 0) {
            // Same name, check if the location is close
            float distance = haversine_distance(
                existing->center.latitude, existing->center.longitude,
                zone->center.latitude, zone->center.longitude);
            
            if (distance < 100.0f) {  // Within 100 meters
                // Update the existing zone
                memcpy(existing, zone, sizeof(rescue_zone_t));
                if (DEBUG) printf("Updated existing rescue zone: %s\n", zone->name);
                return;
            }
        }
    }
    
    // Add new zone if we have space
    if (state->map_system.zone_count < MAX_ZONES) {
        memcpy(&state->map_system.rescue_zones[state->map_system.zone_count], 
              zone, sizeof(rescue_zone_t));
        state->map_system.zone_count++;
        
        if (DEBUG) printf("Added new rescue zone: %s\n", zone->name);
    } else {
        if (DEBUG) printf("No space for new rescue zone\n");
    }
}

/**
 * Broadcast a rescue zone to the network
 */
void broadcast_rescue_zone(system_state_t* state, rescue_zone_t* zone) {
    if (DEBUG) printf("Broadcasting rescue zone: %s\n", zone->name);
    
    // Pack the zone data
    uint8_t payload[sizeof(rescue_zone_t)];
    memcpy(payload, zone, sizeof(rescue_zone_t));
    
    // Broadcast with high priority
    send_message(state, 0xFFFF, MSG_ZONE, payload, sizeof(rescue_zone_t), PRIORITY_HIGH);
}

/**
 * Process a received rescue zone message
 */
void process_zone_message(system_state_t* state, message_t* msg) {
    if (msg->payload_len < sizeof(rescue_zone_t)) return;
    
    // Extract zone data
    rescue_zone_t zone;
    memcpy(&zone, msg->payload, sizeof(rescue_zone_t));
    
    if (DEBUG) {
        char loc_str[128];
        format_location_string(&zone.center, loc_str, sizeof(loc_str));
        printf("Received rescue zone: %s at %s, radius %.1f m\n", 
              zone.name, loc_str, zone.radius);
    }
    
    // Add to our database
    add_rescue_zone(state, &zone);
}

/**
 * Check if a location is within a rescue zone
 */
bool is_in_rescue_zone(geo_location_t* location, rescue_zone_t* zone) {
    float distance = haversine_distance(
        location->latitude, location->longitude,
        zone->center.latitude, zone->center.longitude);
    
    return distance <= zone->radius;
}

/* ---- Mesh Implementation ---- */

/**
 * Initialize the system state
 */
void system_init(system_state_t* state, hal_t hal, node_id_t node_id) {
    memset(state, 0, sizeof(system_state_t));
    state->hal = hal;
    state->node_id = node_id;
    state->outbound_queue = NULL;
    state->outbound_queue_size = 0;
    state->last_beacon_time = 0;
    state->battery_level = state->hal.get_battery_level();
    state->current_rf_channel = 0;
    state->low_power_mode = false;
    state->next_msg_id = 0;
    
    // Initialize hardware
    state->hal.init_radio();
    state->hal.set_rf_channel(state->current_rf_channel);
    
    // Initialize map system
    init_map_system(&state->map_system);
    
    // Generate a temporary random location if GPS is not available
    update_gps_location(state);
    if (state->location.fix_quality == GPS_FIX_NONE) {
        // No GPS fix, use simulated position for testing
        state->location.latitude = 37.7749 + ((double)rand() / RAND_MAX) * 0.01;
        state->location.longitude = -122.4194 + ((double)rand() / RAND_MAX) * 0.01;
        state->location.altitude = 10.0 + (rand() % 100);
        state->location.timestamp = state->hal.get_time_ms();
        state->location.fix_quality = GPS_FIX_SIMULATION;
        state->location.satellites = 0;
    }
    
    // Initialize with a random message ID
    for (int i = 0; i < 4; i++) {
        state->next_msg_id = (state->next_msg_id << 8) | state->hal.get_random_byte();
    }
    
    // Use fixed encryption key for all nodes (in a real system, this would be securely distributed)
    uint8_t key[ENCRYPTION_KEY_SIZE];
    memset(key, 0, ENCRYPTION_KEY_SIZE);
    strncpy((char*)key, SHARED_KEY, ENCRYPTION_KEY_SIZE);
    set_encryption_key(state, key);
}

/**
 * Set the encryption key for secure communications
 */
void set_encryption_key(system_state_t* state, const uint8_t* key) {
    memcpy(state->encryption_key, key, ENCRYPTION_KEY_SIZE);
}

/**
 * Update the node's geographic location
 */
void set_location(system_state_t* state, geo_location_t location) {
    state->location = location;
    
    // Update in our location database too
    update_node_location(state, state->node_id, &location);
}

/**
 * Process all incoming messages
 */
void process_incoming_messages(system_state_t* state) {
    uint8_t buffer[sizeof(message_t)];
    uint16_t len = state->hal.receive_packet(buffer, sizeof(buffer));
    
    if (len > 0 && len >= sizeof(message_t)) {
        message_t* msg = (message_t*)buffer;
        
        if (DEBUG) {
            printf("Received message type: %s from node %d to node %d (hop count: %d)\n", 
                  get_message_type_name(msg->type), msg->source, msg->destination, msg->hop_count);
        }
        
        // Validate the message
        if (msg->type >= MSG_BEACON && msg->type <= MSG_ZONE) {
            // Verify message integrity
            if (verify_mac(state, msg)) {
                // Decrypt message if necessary
                if ((msg->type == MSG_DATA || msg->type == MSG_ALERT) && 
                    !decrypt_message(state, msg)) {
                    if (DEBUG) printf("Failed to decrypt message\n");
                    return; // Failed to decrypt
                }
                
                // Don't process our own messages
                if (msg->source == state->node_id) {
                    if (DEBUG) printf("Ignoring our own message\n");
                    return;
                }
                
                // Process the message
                if (DEBUG) printf("Processing message...\n");
                process_message(state, msg);
            } else {
                if (DEBUG) printf("MAC verification failed\n");
            }
        } else {
            if (DEBUG) printf("Invalid message type: %d\n", msg->type);
        }
    }
}

/**
 * Send a beacon message to announce presence on the network
 */
void send_beacon(system_state_t* state) {
    uint32_t current_time = state->hal.get_time_ms();
    
    // Only send beacon at configured intervals
    if (current_time - state->last_beacon_time >= BEACON_INTERVAL_MS) {
        if (DEBUG) printf("Sending beacon...\n");
        
        message_t beacon;
        memset(&beacon, 0, sizeof(message_t));
        
        beacon.type = MSG_BEACON;
        beacon.source = state->node_id;
        beacon.destination = 0xFFFF; // Broadcast
        beacon.id = state->next_msg_id++;
        beacon.hop_count = 0;
        beacon.ttl = 1; // Beacons only travel 1 hop
        beacon.priority = PRIORITY_LOW;
        beacon.timestamp = current_time;
        
        // Include current node information in the payload
        uint8_t* p = beacon.payload;
        memcpy(p, &state->battery_level, sizeof(battery_level_t));
        p += sizeof(battery_level_t);
        
        memcpy(p, &state->location, sizeof(geo_location_t));
        p += sizeof(geo_location_t);
        
        uint8_t node_capabilities = 0;
        if (state->battery_level > BATTERY_LOW_THRESHOLD) {
            node_capabilities |= 0x01; // Can relay
        }
        *p++ = node_capabilities;
        
        *p++ = state->current_rf_channel;
        
        beacon.payload_len = p - beacon.payload;
        
        // Calculate MAC
        calculate_mac(state, &beacon);
        
        // Send the beacon
        if (state->hal.send_packet((uint8_t*)&beacon, sizeof(message_t), 1)) {
            state->last_beacon_time = current_time;
            if (DEBUG) printf("Beacon sent successfully\n");
        } else {
            if (DEBUG) printf("Failed to send beacon\n");
        }
    }
}

/**
 * Update information about a known node
 */
void update_node_info(system_state_t* state, node_id_t id, signal_strength_t signal, 
                     battery_level_t battery, hop_count_t hops, geo_location_t* location) {
    // Don't add ourselves to the known nodes list
    if (id == state->node_id) {
        return;
    }
    
    node_info_t* node = find_node(state, id);
    
    if (node) {
        // Update existing node
        node->last_seen = state->hal.get_time_ms();
        node->signal_strength = signal;
        node->battery_level = battery;
        node->hop_count = hops;
        if (location) {
            node->location = *location;
            
            // Also update the location in our database
            update_node_location(state, id, location);
        }
        node->is_relay = (battery > BATTERY_LOW_THRESHOLD);
        
        if (DEBUG) printf("Updated node %d information\n", id);
    } else if (state->node_count < MAX_NODES) {
        // Add new node
        node = &state->known_nodes[state->node_count++];
        node->id = id;
        node->last_seen = state->hal.get_time_ms();
        node->signal_strength = signal;
        node->battery_level = battery;
        node->hop_count = hops;
        if (location) {
            node->location = *location;
            
            // Also add to our location database
            update_node_location(state, id, location);
        } else {
            memset(&node->location, 0, sizeof(geo_location_t));
        }
        node->is_relay = (battery > BATTERY_LOW_THRESHOLD);
        
        if (DEBUG) printf("Added new node %d to known nodes list\n", id);
    } else {
        if (DEBUG) printf("Cannot add node %d, known nodes list is full\n", id);
    }
}

/**
 * Find a node in the known nodes list
 */
node_info_t* find_node(system_state_t* state, node_id_t id) {
    for (int i = 0; i < state->node_count; i++) {
        if (state->known_nodes[i].id == id) {
            return &state->known_nodes[i];
        }
    }
    return NULL;
}

/**
 * Remove nodes that haven't been seen recently
 */
void clean_stale_nodes(system_state_t* state) {
    uint32_t current_time = state->hal.get_time_ms();
    
    for (int i = 0; i < state->node_count; i++) {
        if (current_time - state->known_nodes[i].last_seen > NODE_TIMEOUT_MS) {
            // Remove the stale node by shifting the array
            if (DEBUG) printf("Removing stale node %d\n", state->known_nodes[i].id);
            
            if (i < state->node_count - 1) {
                memmove(&state->known_nodes[i], &state->known_nodes[i + 1], 
                       (state->node_count - i - 1) * sizeof(node_info_t));
            }
            state->node_count--;
            i--; // Check the same index again since we shifted
        }
    }
}

/**
 * Find a route to the specified destination
 */
route_entry_t* find_route(system_state_t* state, node_id_t destination) {
    for (int i = 0; i < state->route_count; i++) {
        if (state->routing_table[i].destination == destination) {
            return &state->routing_table[i];
        }
    }
    return NULL;
}

/**
 * Add or update a route in the routing table
 */
bool add_or_update_route(system_state_t* state, node_id_t destination, 
                         node_id_t next_hop, hop_count_t hop_count, 
                         signal_strength_t quality) {
    // Don't route to ourselves
    if (destination == state->node_id) {
        return false;
    }
    
    route_entry_t* route = find_route(state, destination);
    
    if (route) {
        // Update existing route if new one is better
        if (hop_count < route->hop_count || 
            (hop_count == route->hop_count && quality > route->signal_quality)) {
            route->next_hop = next_hop;
            route->hop_count = hop_count;
            route->signal_quality = quality;
            route->last_updated = state->hal.get_time_ms();
            if (DEBUG) printf("Updated route to node %d via node %d (%d hops)\n", 
                             destination, next_hop, hop_count);
            return true;
        }
    } else if (state->route_count < MAX_ROUTES) {
        // Add new route
        route = &state->routing_table[state->route_count++];
        route->destination = destination;
        route->next_hop = next_hop;
        route->hop_count = hop_count;
        route->signal_quality = quality;
        route->last_updated = state->hal.get_time_ms();
        if (DEBUG) printf("Added new route to node %d via node %d (%d hops)\n", 
                         destination, next_hop, hop_count);
        return true;
    } else {
        // Routing table is full, try to replace worst route
        route_entry_t* worst_route = &state->routing_table[0];
        for (int i = 1; i < state->route_count; i++) {
            if (state->routing_table[i].hop_count > worst_route->hop_count ||
                (state->routing_table[i].hop_count == worst_route->hop_count && 
                 state->routing_table[i].signal_quality < worst_route->signal_quality)) {
                worst_route = &state->routing_table[i];
            }
        }
        
        if (hop_count < worst_route->hop_count ||
            (hop_count == worst_route->hop_count && quality > worst_route->signal_quality)) {
            worst_route->destination = destination;
            worst_route->next_hop = next_hop;
            worst_route->hop_count = hop_count;
            worst_route->signal_quality = quality;
            worst_route->last_updated = state->hal.get_time_ms();
            if (DEBUG) printf("Replaced worst route with route to node %d via node %d (%d hops)\n", 
                             destination, next_hop, hop_count);
            return true;
        }
    }
    
    return false;
}

/**
 * Send a route request to find a path to a destination
 */
void send_route_request(system_state_t* state, node_id_t destination) {
    message_t req;
    memset(&req, 0, sizeof(message_t));
    
    req.type = MSG_ROUTE_REQUEST;
    req.source = state->node_id;
    req.destination = 0xFFFF; // Broadcast
    req.id = state->next_msg_id++;
    req.hop_count = 0;
    req.ttl = MAX_HOPS;
    req.priority = PRIORITY_NORMAL;
    req.timestamp = state->hal.get_time_ms();
    
    // Set destination in payload
    memcpy(req.payload, &destination, sizeof(node_id_t));
    req.payload_len = sizeof(node_id_t);
    
    // Calculate MAC
    calculate_mac(state, &req);
    
    // Send route request
    if (state->hal.send_packet((uint8_t*)&req, sizeof(message_t), 2)) {
        if (DEBUG) printf("Sent route request for node %d\n", destination);
    } else {
        if (DEBUG) printf("Failed to send route request for node %d\n", destination);
    }
}

/**
 * Process a route request message
 */
void process_route_request(system_state_t* state, message_t* msg) {
    if (msg->payload_len < sizeof(node_id_t)) {
        if (DEBUG) printf("Invalid route request message\n");
        return; // Invalid message
    }
    
    node_id_t requested_dest;
    memcpy(&requested_dest, msg->payload, sizeof(node_id_t));
    
    if (DEBUG) printf("Processing route request for node %d from node %d\n", 
                     requested_dest, msg->source);
    
    // Check if we are the requested destination or know a route to it
    if (requested_dest == state->node_id) {
        // We are the destination, send a route response
        if (DEBUG) printf("We are the requested destination, sending route response\n");
        
        message_t resp;
        memset(&resp, 0, sizeof(message_t));
        
        resp.type = MSG_ROUTE_RESPONSE;
        resp.source = state->node_id;
        resp.destination = msg->source;
        resp.id = state->next_msg_id++;
        resp.hop_count = 0;
        resp.ttl = MAX_HOPS;
        resp.priority = PRIORITY_NORMAL;
        resp.timestamp = state->hal.get_time_ms();
        
        // No payload needed for direct route
        resp.payload_len = 0;
        
        // Calculate MAC
        calculate_mac(state, &resp);
        
        // Send route response
        state->hal.send_packet((uint8_t*)&resp, sizeof(message_t), 2);
    } else {
        route_entry_t* route = find_route(state, requested_dest);
        if (route) {
            // We know a route, send response
            if (DEBUG) printf("We know a route to node %d, sending route response\n", requested_dest);
            
            message_t resp;
            memset(&resp, 0, sizeof(message_t));
            
            resp.type = MSG_ROUTE_RESPONSE;
            resp.source = state->node_id;
            resp.destination = msg->source;
            resp.id = state->next_msg_id++;
            resp.hop_count = 0;
            resp.ttl = MAX_HOPS;
            resp.priority = PRIORITY_NORMAL;
            resp.timestamp = state->hal.get_time_ms();
            
            // Include route information in payload
            uint8_t* p = resp.payload;
            memcpy(p, &requested_dest, sizeof(node_id_t));
            p += sizeof(node_id_t);
            
            memcpy(p, &route->hop_count, sizeof(hop_count_t));
            p += sizeof(hop_count_t);
            
            resp.payload_len = p - resp.payload;
            
            // Calculate MAC
            calculate_mac(state, &resp);
            
            // Send route response
            state->hal.send_packet((uint8_t*)&resp, sizeof(message_t), 2);
        } else if (msg->hop_count < MAX_HOPS - 1) {
            // Forward route request
            if (DEBUG) printf("Forwarding route request for node %d\n", requested_dest);
            
            msg->hop_count++;
            calculate_mac(state, msg);
            state->hal.send_packet((uint8_t*)msg, sizeof(message_t), 2);
        }
    }
}

/**
 * Process a route response message
 */
void process_route_response(system_state_t* state, message_t* msg) {
    if (DEBUG) printf("Processing route response from node %d\n", msg->source);
    
    if (msg->destination == state->node_id) {
        // This response is for us
        if (msg->payload_len >= sizeof(node_id_t)) {
            node_id_t dest_id;
            memcpy(&dest_id, msg->payload, sizeof(node_id_t));
            
            hop_count_t hop_count = 1; // At minimum one hop through sender
            
            if (msg->payload_len >= sizeof(node_id_t) + sizeof(hop_count_t)) {
                memcpy(&hop_count, msg->payload + sizeof(node_id_t), sizeof(hop_count_t));
                hop_count += 1; // Add one for the hop to the sender
            }
            
            // Add/update route through the sender
            signal_strength_t quality = state->hal.get_last_rssi();
            if (add_or_update_route(state, dest_id, msg->source, hop_count, quality)) {
                if (DEBUG) printf("Added/updated route to node %d via node %d (%d hops)\n", 
                                 dest_id, msg->source, hop_count);
            }
        }
    } else {
        // Forward the response
        if (DEBUG) printf("Forwarding route response to node %d\n", msg->destination);
        forward_message(state, msg);
    }
}

/**
 * Remove stale routes from the routing table
 */
void clean_stale_routes(system_state_t* state) {
    uint32_t current_time = state->hal.get_time_ms();
    uint32_t route_timeout = NODE_TIMEOUT_MS * 2; // Routes time out after twice node timeout
    
    for (int i = 0; i < state->route_count; i++) {
        if (current_time - state->routing_table[i].last_updated > route_timeout) {
            // Remove the stale route by shifting the array
            if (DEBUG) printf("Removing stale route to node %d\n", state->routing_table[i].destination);
            
            if (i < state->route_count - 1) {
                memmove(&state->routing_table[i], &state->routing_table[i + 1], 
                       (state->route_count - i - 1) * sizeof(route_entry_t));
            }
            state->route_count--;
            i--; // Check the same index again since we shifted
        }
    }
}

/**
 * Send a new message to a destination
 */
bool send_message(system_state_t* state, node_id_t destination, 
                 message_type_t type, const uint8_t* payload, 
                 uint16_t payload_len, message_priority_t priority) {
    if (payload_len > MAX_MSG_SIZE) {
        if (DEBUG) printf("Payload too large\n");
        return false; // Payload too large
    }
    
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    
    msg.type = type;
    msg.source = state->node_id;
    msg.destination = destination;
    msg.id = state->next_msg_id++;
    msg.hop_count = 0;
    msg.ttl = MAX_HOPS;
    msg.priority = priority;
    msg.timestamp = state->hal.get_time_ms();
    msg.payload_len = payload_len;
    
    if (payload_len > 0 && payload != NULL) {
        memcpy(msg.payload, payload, payload_len);
    }
    
    // Encrypt message if it's a data or alert message
    if (type == MSG_DATA || type == MSG_ALERT) {
        encrypt_message(state, &msg);
    }
    
    // Calculate MAC
    calculate_mac(state, &msg);
    
    // Determine power level based on priority
    uint8_t power_level;
    switch (priority) {
        case PRIORITY_EMERGENCY:
            power_level = 3; // Maximum power
            break;
        case PRIORITY_HIGH:
            power_level = 2; // High power
            break;
        case PRIORITY_NORMAL:
            power_level = 1; // Normal power
            break;
        case PRIORITY_LOW:
        default:
            power_level = 0; // Minimum power
            break;
    }
    
    if (DEBUG) printf("Sending message type %s to node %d\n", 
                     get_message_type_name(type), destination);
    
    // Try to send immediately if we have a direct route or it's a broadcast
    if (destination == 0xFFFF || find_node(state, destination) != NULL) {
        // Direct send
        bool sent = state->hal.send_packet((uint8_t*)&msg, sizeof(message_t), power_level);
        if (sent) {
            if (DEBUG) printf("Message sent directly\n");
            // Queue for ACK if it's a data or alert message
            if ((type == MSG_DATA || type == MSG_ALERT) && destination != 0xFFFF) {
                enqueue_message(state, &msg);
            }
            return true;
        }
    } else {
        // Try using routing table
        route_entry_t* route = find_route(state, destination);
        if (route) {
            // Send to next hop
            bool sent = state->hal.send_packet((uint8_t*)&msg, sizeof(message_t), power_level);
            if (sent) {
                if (DEBUG) printf("Message sent via route\n");
                // Queue for ACK if it's a data or alert message
                if (type == MSG_DATA || type == MSG_ALERT) {
                    enqueue_message(state, &msg);
                }
                return true;
            }
        }
    }
    
    // If we don't have a route or sending failed, initiate route discovery and queue
    if (destination != 0xFFFF) {  // Don't need routes for broadcast
        if (DEBUG) printf("No route available, initiating route discovery\n");
        send_route_request(state, destination);
    }
    
    enqueue_message(state, &msg);
    
    return true;
}

/**
 * Forward a message to its next hop
 */
void forward_message(system_state_t* state, message_t* msg) {
    // Don't forward if TTL is 0 or we're the source
    if (msg->ttl == 0 || msg->source == state->node_id) {
        if (DEBUG) printf("Not forwarding message (TTL: %d, source: %d)\n", 
                         msg->ttl, msg->source);
        return;
    }
    
    // Don't forward if we've already seen this message (loop prevention)
    for (msg_queue_entry_t* entry = state->outbound_queue; entry != NULL; entry = entry->next) {
        if (entry->message.source == msg->source && 
            entry->message.id == msg->id) {
            if (DEBUG) printf("Not forwarding message (already seen)\n");
            return; // Already seen this message
        }
    }
    
    // Decrement TTL
    msg->ttl--;
    msg->hop_count++;
    
    // Recalculate MAC
    calculate_mac(state, msg);
    
    if (DEBUG) printf("Forwarding message type %s from node %d to node %d (hop count: %d, TTL: %d)\n", 
                     get_message_type_name(msg->type), msg->source, msg->destination, 
                     msg->hop_count, msg->ttl);
    
    // If it's a broadcast, just forward it
    if (msg->destination == 0xFFFF) {
        // Use lower power for broadcasts to avoid congestion
        if (state->hal.send_packet((uint8_t*)msg, sizeof(message_t), 1)) {
            if (DEBUG) printf("Broadcast message forwarded\n");
        } else {
            if (DEBUG) printf("Failed to forward broadcast message\n");
        }
    } else {
        // Look up next hop
        route_entry_t* route = find_route(state, msg->destination);
        if (route) {
            // Send to next hop
            if (state->hal.send_packet((uint8_t*)msg, sizeof(message_t), 1)) {
                if (DEBUG) printf("Message forwarded to next hop (node %d)\n", route->next_hop);
            } else {
                if (DEBUG) printf("Failed to forward message to next hop\n");
            }
        } else {
            // We don't know how to reach the destination
            // Start route discovery and queue the message
            if (DEBUG) printf("No route for forwarding, initiating route discovery\n");
            send_route_request(state, msg->destination);
            enqueue_message(state, msg);
        }
    }
}

/**
 * Process a received message
 */
void process_message(system_state_t* state, message_t* msg) {
    // Check if message is valid
    if (msg == NULL) {
        return;
    }
    
    // Update route information based on received message
    signal_strength_t quality = state->hal.get_last_rssi();
    add_or_update_route(state, msg->source, msg->source, 1, quality);
    
    if (DEBUG) printf("Processing message type %s from node %d\n", 
                     get_message_type_name(msg->type), msg->source);
    
    // Process based on message type
    switch (msg->type) {
        case MSG_BEACON:
            if (msg->payload_len >= sizeof(battery_level_t) + sizeof(geo_location_t)) {
                battery_level_t battery;
                geo_location_t location;
                
                uint8_t* p = msg->payload;
                memcpy(&battery, p, sizeof(battery_level_t));
                p += sizeof(battery_level_t);
                
                memcpy(&location, p, sizeof(geo_location_t));
                
                // Update node information
                update_node_info(state, msg->source, quality, battery, msg->hop_count, &location);
                
                // Estimate distance based on signal strength
                node_info_t* node = find_node(state, msg->source);
                if (node) {
                    estimate_node_distance(state, msg->source);
                    
                    // If the node doesn't have a GPS fix, try to estimate its position
                    if (node->location.fix_quality == GPS_FIX_NONE) {
                        triangulate_position(state, msg->source);
                    }
                }
            }
            break;
            
        case MSG_DATA:
        case MSG_ALERT:
            if (msg->destination == state->node_id) {
                // Message is for us
                printf("Received message from node %d: ", msg->source);
                printf("%s\n", msg->payload); // Assume it's a null-terminated string
                
                // Send acknowledgment
                acknowledge_message(state, msg);
            } else if (msg->destination == 0xFFFF) {
                // Broadcast message
                printf("Received broadcast from node %d: ", msg->source);
                printf("%s\n", msg->payload);
                
                // No need to acknowledge broadcasts
            } else {
                // Message for someone else, forward it
                forward_message(state, msg);
            }
            break;
            
        case MSG_ACK:
            // Got an acknowledgment, remove from queue
            if (DEBUG) printf("Received ACK for message %d from node %d\n", 
                             msg->id, msg->source);
            remove_from_queue(state, msg->id, msg->source);
            break;
            
        case MSG_ROUTE_REQUEST:
            process_route_request(state, msg);
            break;
            
        case MSG_ROUTE_RESPONSE:
            process_route_response(state, msg);
            break;
            
        case MSG_PING:
            if (msg->destination == state->node_id) {
                // Respond with pong
                if (DEBUG) printf("Received ping from node %d, sending pong\n", msg->source);
                
                message_t pong;
                memset(&pong, 0, sizeof(message_t));
                
                pong.type = MSG_PONG;
                pong.source = state->node_id;
                pong.destination = msg->source;
                pong.id = state->next_msg_id++;
                pong.hop_count = 0;
                pong.ttl = MAX_HOPS;
                pong.priority = PRIORITY_LOW;
                pong.timestamp = state->hal.get_time_ms();
                
                // Copy ping payload to pong
                if (msg->payload_len > 0) {
                    memcpy(pong.payload, msg->payload, msg->payload_len);
                    pong.payload_len = msg->payload_len;
                }
                
                // Calculate MAC
                calculate_mac(state, &pong);
                
                // Send pong
                state->hal.send_packet((uint8_t*)&pong, sizeof(message_t), 1);
            } else {
                // Forward ping
                forward_message(state, msg);
            }
            break;
            
        case MSG_PONG:
            if (msg->destination == state->node_id) {
                // Pong response received
                // Calculate round-trip time
                uint32_t rtt = state->hal.get_time_ms() - msg->timestamp;
                
                printf("Received pong from node %d, RTT: %d ms\n", msg->source, rtt);
                
                // Update route quality based on RTT
                route_entry_t* route = find_route(state, msg->source);
                if (route) {
                    // Lower RTT = better quality (scale is implementation dependent)
                    signal_strength_t new_quality = (rtt < 100) ? 255 : (uint8_t)(25500 / rtt);
                    if (new_quality > route->signal_quality) {
                        route->signal_quality = new_quality;
                        route->last_updated = state->hal.get_time_ms();
                    }
                }
                
                // Update RTT in node info for distance estimation
                node_info_t* node = find_node(state, msg->source);
                if (node) {
                    node->rtt = rtt;
                    node->last_ping_time = state->hal.get_time_ms();
                    
                    // Update distance estimate based on new RTT
                    estimate_node_distance(state, msg->source);
                }
            } else {
                // Forward pong
                forward_message(state, msg);
            }
            break;
            
        case MSG_LOCATION:
            // Process location update
            process_location_message(state, msg);
            break;
            
        case MSG_MAP_REQUEST:
            // Process map tile request
            process_map_request(state, msg);
            break;
            
        case MSG_MAP_DATA:
            // Process received map tile data
            process_map_data(state, msg);
            break;
            
        case MSG_POI:
            // Process point of interest
            process_poi_message(state, msg);
            break;
            
        case MSG_ZONE:
            // Process rescue zone
            process_zone_message(state, msg);
            break;
    }
}

/**
 * Add a message to the outbound queue
 */
void enqueue_message(system_state_t* state, message_t* msg) {
    // Check if queue is full
    if (state->outbound_queue_size >= MAX_QUEUE_SIZE) {
        // Find lowest priority message to replace
        msg_queue_entry_t* lowest_entry = NULL;
        msg_queue_entry_t* prev_entry = NULL;
        msg_queue_entry_t* curr = state->outbound_queue;
        msg_queue_entry_t* prev = NULL;
        
        while (curr != NULL) {
            if (lowest_entry == NULL || curr->message.priority < lowest_entry->message.priority) {
                lowest_entry = curr;
                prev_entry = prev;
            }
            prev = curr;
            curr = curr->next;
        }
        
        // If new message is higher priority than lowest, replace it
        if (lowest_entry != NULL && msg->priority > lowest_entry->message.priority) {
            if (prev_entry == NULL) {
                // Lowest is at head of queue
                state->outbound_queue = lowest_entry->next;
            } else {
                prev_entry->next = lowest_entry->next;
            }
            free(lowest_entry);
            state->outbound_queue_size--;
        } else {
            // Queue is full and new message is not higher priority
            if (DEBUG) printf("Queue full, dropping message\n");
            return;
        }
    }
    
    // Allocate new queue entry
    msg_queue_entry_t* entry = (msg_queue_entry_t*)malloc(sizeof(msg_queue_entry_t));
    if (entry == NULL) {
        if (DEBUG) printf("Out of memory, dropping message\n");
        return; // Out of memory
    }
    
    // Copy message
    memcpy(&entry->message, msg, sizeof(message_t));
    entry->next_retry = state->hal.get_time_ms() + RETRY_INTERVAL_MS;
    entry->retry_count = 0;
    entry->awaiting_ack = (msg->type == MSG_DATA || msg->type == MSG_ALERT) && msg->destination != 0xFFFF;
    entry->next = NULL;
    
    if (DEBUG) printf("Enqueuing message type %s to node %d (awaiting ACK: %d)\n", 
                     get_message_type_name(msg->type), msg->destination, entry->awaiting_ack);
    
    // Add to queue
    if (state->outbound_queue == NULL) {
        state->outbound_queue = entry;
    } else {
        // Add based on priority
        if (entry->message.priority > state->outbound_queue->message.priority) {
            // New entry is higher priority than head
            entry->next = state->outbound_queue;
            state->outbound_queue = entry;
        } else {
            // Find insertion point
            msg_queue_entry_t* curr = state->outbound_queue;
            while (curr->next != NULL && curr->next->message.priority >= entry->message.priority) {
                curr = curr->next;
            }
            entry->next = curr->next;
            curr->next = entry;
        }
    }
    
    state->outbound_queue_size++;
}

/**
 * Process the outbound message queue
 */
void process_queue(system_state_t* state) {
    uint32_t current_time = state->hal.get_time_ms();
    
    msg_queue_entry_t* entry = state->outbound_queue;
    msg_queue_entry_t* prev = NULL;
    
    while (entry != NULL) {
        if (current_time >= entry->next_retry) {
            // Time to retry or send for the first time
            bool send_success = false;
            
            if (DEBUG) printf("Processing queued message type %s to node %d (retry: %d)\n", 
                             get_message_type_name(entry->message.type), 
                             entry->message.destination, entry->retry_count);
            
            // Check if we have a route for non-broadcast messages
            if (entry->message.destination == 0xFFFF) {
                // Broadcasts don't need routes
                send_success = true;
            } else {
                route_entry_t* route = find_route(state, entry->message.destination);
                if (route) {
                    send_success = true;
                } else if (find_node(state, entry->message.destination) != NULL) {
                    // Direct node
                    send_success = true;
                }
            }
            
            if (send_success) {
                // Determine power level based on priority and retry count
                uint8_t power_level = entry->message.priority;
                if (entry->retry_count > 0) {
                    power_level++; // Increase power for retries
                }
                if (power_level > 3) {
                    power_level = 3; // Max power level
                }
                
                // Send packet
                send_success = state->hal.send_packet((uint8_t*)&entry->message, 
                                                    sizeof(message_t), power_level);
                
                if (DEBUG) {
                    if (send_success) {
                        printf("Queue message sent successfully\n");
                    } else {
                        printf("Failed to send queued message\n");
                    }
                }
            }
            
            if (send_success) {
                if (!entry->awaiting_ack) {
                    // If not waiting for ACK, remove from queue
                    if (DEBUG) printf("Message doesn't need ACK, removing from queue\n");
                    
                    msg_queue_entry_t* to_remove = entry;
                    
                    if (prev == NULL) {
                        state->outbound_queue = entry->next;
                    } else {
                        prev->next = entry->next;
                    }
                    
                    entry = entry->next;
                    free(to_remove);
                    state->outbound_queue_size--;
                    continue;
                } else {
                    // Update retry info
                    entry->retry_count++;
                    entry->next_retry = current_time + RETRY_INTERVAL_MS * entry->retry_count;
                    
                    // If max retries reached, give up
                    if (entry->retry_count >= 5) {
                        if (DEBUG) printf("Max retries reached, removing message from queue\n");
                        
                        msg_queue_entry_t* to_remove = entry;
                        
                        if (prev == NULL) {
                            state->outbound_queue = entry->next;
                        } else {
                            prev->next = entry->next;
                        }
                        
                        entry = entry->next;
                        free(to_remove);
                        state->outbound_queue_size--;
                        continue;
                    }
                }
            } else {
                // Sending failed, may need to initiate route discovery
                if (entry->message.destination != 0xFFFF) { // Not for broadcasts
                    if (DEBUG) printf("Sending failed, initiating route discovery\n");
                    send_route_request(state, entry->message.destination);
                }
                
                // Set retry time
                entry->next_retry = current_time + RETRY_INTERVAL_MS;
            }
        }
        
        prev = entry;
        entry = entry->next;
    }
}

/**
 * Send an acknowledgment for a received message
 */
void acknowledge_message(system_state_t* state, message_t* msg) {
    message_t ack;
    memset(&ack, 0, sizeof(message_t));
    
    ack.type = MSG_ACK;
    ack.source = state->node_id;
    ack.destination = msg->source;
    ack.id = msg->id; // Use same ID as original message
    ack.hop_count = 0;
    ack.ttl = MAX_HOPS;
    ack.priority = PRIORITY_HIGH; // ACKs are high priority
    ack.timestamp = state->hal.get_time_ms();
    
    // No payload for ACKs
    ack.payload_len = 0;
    
    // Calculate MAC
    calculate_mac(state, &ack);
    
    if (DEBUG) printf("Sending ACK for message %d to node %d\n", msg->id, msg->source);
    
    // Send ACK immediately
    if (state->hal.send_packet((uint8_t*)&ack, sizeof(message_t), 2)) {
        if (DEBUG) printf("ACK sent successfully\n");
    } else {
        if (DEBUG) printf("Failed to send ACK\n");
    }
}

/**
 * Remove a message from the queue after receiving an ACK
 */
void remove_from_queue(system_state_t* state, msg_id_t id, node_id_t source) {
    msg_queue_entry_t* entry = state->outbound_queue;
    msg_queue_entry_t* prev = NULL;
    
    while (entry != NULL) {
        if (entry->message.id == id && entry->message.destination == source) {
            // Found the message to remove
            if (DEBUG) printf("Removing message %d from queue (ACK received)\n", id);
            
            if (prev == NULL) {
                state->outbound_queue = entry->next;
            } else {
                prev->next = entry->next;
            }
            
            free(entry);
            state->outbound_queue_size--;
            return;
        }
        
        prev = entry;
        entry = entry->next;
    }
}

/**
 * Simplified MAC calculation without using offsetof
 */
void calculate_mac(system_state_t* state, message_t* msg) {
    if (msg == NULL) return;
    
    // Initialize MAC with values derived from key
    for (int i = 0; i < MAC_SIZE; i++) {
        msg->mac[i] = state->encryption_key[i % ENCRYPTION_KEY_SIZE];
    }
    
    // Mix in header fields individually
    
    // Mix in type
    msg->mac[0] ^= msg->type;
    
    // Mix in source and destination
    for (int i = 0; i < sizeof(node_id_t); i++) {
        msg->mac[1 + i % (MAC_SIZE-1)] ^= ((uint8_t*)&msg->source)[i];
        msg->mac[2 + i % (MAC_SIZE-2)] ^= ((uint8_t*)&msg->destination)[i];
    }
    
    // Mix in message ID
    msg->mac[3] ^= msg->id;
    
    // Mix in hop count and TTL
    msg->mac[4] ^= msg->hop_count;
    msg->mac[5] ^= msg->ttl;
    
    // Mix in priority
    msg->mac[6] ^= msg->priority;
    
// Mix in timestamp
    for (int i = 0; i < sizeof(uint32_t); i++) {
        msg->mac[7 + i % (MAC_SIZE-7)] ^= ((uint8_t*)&msg->timestamp)[i];
    }
    
    // Mix in payload length
    for (int i = 0; i < sizeof(uint16_t); i++) {
        msg->mac[8 + i % (MAC_SIZE-8)] ^= ((uint8_t*)&msg->payload_len)[i];
    }
    
    // Mix in payload
    for (int i = 0; i < msg->payload_len && i < MAX_MSG_SIZE; i++) {
        msg->mac[i % MAC_SIZE] ^= msg->payload[i];
    }
    
    // Final scrambling round
    for (int i = 0; i < MAC_SIZE/2; i++) {
        uint8_t temp = msg->mac[i];
        msg->mac[i] ^= msg->mac[MAC_SIZE-1-i] ^ state->encryption_key[(i*3) % ENCRYPTION_KEY_SIZE];
        msg->mac[MAC_SIZE-1-i] ^= temp ^ state->encryption_key[(i*7) % ENCRYPTION_KEY_SIZE];
    }
}

/**
 * Verify message MAC without using offsetof
 */
bool verify_mac(system_state_t* state, message_t* msg) {
    if (msg == NULL) {
        return false;
    }
    
    // Save original MAC
    uint8_t original_mac[MAC_SIZE];
    memcpy(original_mac, msg->mac, MAC_SIZE);
    
    // Clear MAC field for calculation
    memset(msg->mac, 0, MAC_SIZE);
    
    // Calculate new MAC
    calculate_mac(state, msg);
    
    // Compare MACs
    bool result = true;
    for (int i = 0; i < MAC_SIZE; i++) {
        if (msg->mac[i] != original_mac[i]) {
            result = false;
            break;
        }
    }
    
    // Restore original MAC regardless of result
    memcpy(msg->mac, original_mac, MAC_SIZE);
    
    return result;
}

/**
 * Encrypt a message payload
 * 
 * Note: In a real implementation, this would use a proper encryption algorithm
 * like AES-GCM with a properly managed key.
 */
void encrypt_message(system_state_t* state, message_t* msg) {
    // Simple XOR encryption for demonstration purposes
    // In a real implementation, use a secure encryption algorithm
    for (int i = 0; i < msg->payload_len; i++) {
        msg->payload[i] ^= state->encryption_key[i % ENCRYPTION_KEY_SIZE];
    }
}

/**
 * Decrypt a message payload
 */
bool decrypt_message(system_state_t* state, message_t* msg) {
    // For XOR, encryption and decryption are the same operation
    encrypt_message(state, msg);
    return true;
}

/**
 * Check battery status and adjust behavior accordingly
 */
void check_battery_status(system_state_t* state) {
    state->battery_level = state->hal.get_battery_level();
    
    if (state->battery_level <= BATTERY_CRIT_THRESHOLD) {
        // Critical battery level
        // Only process emergency messages
        // Enter low power mode when possible
        enter_low_power_mode(state);
    } else if (state->battery_level <= BATTERY_LOW_THRESHOLD) {
        // Low battery
        // Reduce beacon frequency
        // Don't act as a relay for non-emergency messages
    }
}

/**
 * Adapt power settings based on network conditions
 */
void adapt_power_settings(system_state_t* state) {
    // If we have many nearby nodes, reduce transmission power
    int nearby_nodes = 0;
    for (int i = 0; i < state->node_count; i++) {
        if (state->known_nodes[i].hop_count == 1 && 
            state->known_nodes[i].signal_strength > 200) {
            nearby_nodes++;
        }
    }
    
    // Adjust beacon interval based on node density
    if (nearby_nodes > 10) {
        // High density, reduce beacon frequency
    } else if (nearby_nodes < 3) {
        // Low density, increase beacon frequency
    }
}

/**
 * Enter low power mode
 */
void enter_low_power_mode(system_state_t* state) {
    if (!state->low_power_mode) {
        state->low_power_mode = true;
        state->hal.enter_low_power_mode();
    }
}

/**
 * Exit low power mode
 */
void exit_low_power_mode(system_state_t* state) {
    if (state->low_power_mode) {
        state->low_power_mode = false;
        state->hal.exit_low_power_mode();
    }
}

/**
 * Scan all RF channels to find the least congested one
 */
void scan_channels(system_state_t* state) {
    uint8_t signal_levels[RF_CHANNEL_COUNT] = {0};
    
    // Scan each channel
    for (uint8_t channel = 0; channel < RF_CHANNEL_COUNT; channel++) {
        state->hal.set_rf_channel(channel);
        
        // Wait for a short period to sample the channel
        state->hal.sleep_ms(10);
        
        // Get signal strength (higher value = more congestion)
        signal_levels[channel] = state->hal.get_last_rssi();
    }
    
    // Find least congested channel
    uint8_t best_channel = 0;
    uint8_t lowest_signal = 255;
    
    for (uint8_t channel = 0; channel < RF_CHANNEL_COUNT; channel++) {
        if (signal_levels[channel] < lowest_signal) {
            lowest_signal = signal_levels[channel];
            best_channel = channel;
        }
    }
    
    // Switch to best channel
    if (best_channel != state->current_rf_channel) {
        switch_channel(state, best_channel);
    }
}

/**
 * Switch to a different RF channel
 */
void switch_channel(system_state_t* state, uint8_t channel) {
    if (channel < RF_CHANNEL_COUNT) {
        state->hal.set_rf_channel(channel);
        state->current_rf_channel = channel;
    }
}

/* ---- Bluetooth HAL Implementation ---- */

/**
 * Enable Bluetooth on the MacBook
 */
void bt_init_radio(void) {
    // Make sure Bluetooth is enabled using blueutil
    system("blueutil --power 1");
    printf("Enabling Bluetooth...\n");
    
    // Create a UDP socket to simulate Bluetooth communication
    // This allows mesh network to work between multiple processes on the same machine
    bt_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (bt_socket < 0) {
        perror("Failed to create socket");
        return;
    }
    
    // Enable broadcast
    int broadcast = 1;
    if (setsockopt(bt_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        perror("Failed to set broadcast option");
    }
    
    // Set up socket address structure
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(bt_port);
    
    // Bind the socket
    if (bind(bt_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Failed to bind socket");
        close(bt_socket);
        bt_socket = -1;
        return;
    }
    
    // Set socket to non-blocking mode
    int flags = fcntl(bt_socket, F_GETFL, 0);
    fcntl(bt_socket, F_SETFL, flags | O_NONBLOCK);
    
    printf("Bluetooth radio initialized on port %d\n", bt_port);
    
    // Scan for devices
    bt_scan_for_devices();
}

/**
 * Scan for nearby Bluetooth devices 
 */
void bt_scan_for_devices(void) {
    // Use blueutil to scan for real Bluetooth devices
    printf("Scanning for Bluetooth devices...\n");
    
    // Reset device count
    bt_device_count = 0;
    
    // Add virtual nodes at different ports
    for (int i = 0; i < 3; i++) {
        int target_port = bt_port + i - 1;  // Include ports before and after current port
        
        // Skip our own port
        if (target_port == bt_port) {
            continue;
        }
        
        // Skip negative ports
        if (target_port <= 0) {
            continue;
        }
        
        snprintf(known_bt_devices[bt_device_count].addr, 18, "127.0.0.1:%d", target_port);
        snprintf(known_bt_devices[bt_device_count].name, 248, "Virtual Node %d", target_port);
        known_bt_devices[bt_device_count].rssi = 80 - (abs(target_port - bt_port) * 10); // Simulated RSSI
        known_bt_devices[bt_device_count].connected = 0;
        
        printf("Found virtual device: %s (%s) RSSI: %d\n", 
               known_bt_devices[bt_device_count].name, 
               known_bt_devices[bt_device_count].addr, 
               known_bt_devices[bt_device_count].rssi);
        
        bt_device_count++;
    }
    
    printf("Found %d devices\n", bt_device_count);
}

/**
 * Send a packet via simulated Bluetooth
 */
bool bt_send_packet(uint8_t* data, uint16_t len, uint8_t power_level) {
    bool success = false;
    
    // Create message_t pointer for debug info
    message_t* msg = (message_t*)data;
    
    if (DEBUG && len >= sizeof(message_t)) {
        printf("Sending packet of type %s from node %d to node %d\n", 
               get_message_type_name(msg->type), msg->source, msg->destination);
    }
    
    // Send to all known devices
    for (int i = 0; i < bt_device_count; i++) {
        // Skip devices that are too far away based on power level
        if (known_bt_devices[i].rssi < 40 + power_level * 10) {
            if (DEBUG) printf("Device %s is too far for current power level\n", 
                             known_bt_devices[i].name);
            continue; // Device is too far for current power level
        }
        
        // Parse the address to get IP and port
        char ip[16];
        int port;
        if (sscanf(known_bt_devices[i].addr, "%15[^:]:%d", ip, &port) != 2) {
            if (DEBUG) printf("Invalid address format: %s\n", known_bt_devices[i].addr);
            continue; // Invalid address format
        }
        
        // Set up destination address
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_addr.s_addr = inet_addr(ip);
        dest_addr.sin_port = htons(port);
        
        // Send the data
        ssize_t sent = sendto(bt_socket, data, len, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        if (sent == len) {
            printf("Sent packet to %s\n", known_bt_devices[i].name);
            success = true;
        } else {
            perror("Failed to send packet");
            if (DEBUG) {
                printf("sendto error: %s (errno: %d)\n", strerror(errno), errno);
                printf("Destination: %s:%d\n", ip, port);
            }
        }
    }
    
    return success;
}

/**
 * Receive a packet via simulated Bluetooth
 */
uint16_t bt_receive_packet(uint8_t* buffer, uint16_t max_len) {
    // Check if we have data in the buffer
    if (bt_rx_buffer_len > 0 && bt_rx_buffer_len <= max_len) {
        memcpy(buffer, bt_rx_buffer, bt_rx_buffer_len);
        uint16_t len = bt_rx_buffer_len;
        bt_rx_buffer_len = 0;
        return len;
    }
    
    // Try to receive data
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    int received = recvfrom(bt_socket, buffer, max_len, 0, 
                          (struct sockaddr*)&sender_addr, &sender_len);
    
    if (received > 0) {
        // Log packet receipt
        if (DEBUG && received >= sizeof(message_t)) {
            message_t* msg = (message_t*)buffer;
            printf("Received packet from %s:%d, type: %s, source: %d, dest: %d, size: %d\n",
                  inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                  get_message_type_name(msg->type), msg->source, msg->destination, received);
        }
        
        // Update simulated RSSI
        bt_last_rssi = 70; // Default RSSI value
        
        // Find the device in our list and update its RSSI
        char addr_str[18];
        snprintf(addr_str, sizeof(addr_str), "%s:%d", 
                inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port));
        
        for (int i = 0; i < bt_device_count; i++) {
            if (strcmp(known_bt_devices[i].addr, addr_str) == 0) {
                bt_last_rssi = known_bt_devices[i].rssi;
                break;
            }
        }
        
        return received;
    }
    
    return 0;
}

/**
 * Sleep for the specified number of milliseconds
 */
void bt_sleep_ms(uint32_t ms) {
    usleep(ms * 1000);
}

/**
 * Get current time in milliseconds
 */
uint32_t bt_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

/**
 * Get the MacBook's battery level
 */
battery_level_t bt_get_battery_level(void) {
    FILE *fp;
    char line[100];
    int battery_level = 100;
    
    // Try to get battery level from system
    fp = popen("pmset -g batt | grep -Eo '\\d+%' | cut -d% -f1", "r");
    if (fp != NULL) {
        if (fgets(line, sizeof(line), fp) != NULL) {
            battery_level = atoi(line);
        }
        pclose(fp);
    }
    
    return (battery_level_t)battery_level;
}

/**
 * Set LED (we'll just print status)
 */
void bt_set_led(bool state) {
    printf("LED: %s\n", state ? "ON" : "OFF");
}

/**
 * Get button state (always return false for now)
 */
bool bt_get_button_state(uint8_t button_id) {
    return false;
}

/**
 * Enter low power mode
 */
void bt_enter_low_power_mode(void) {
    printf("Entering low power mode\n");
    // Reduce scanning frequency
}

/**
 * Exit low power mode
 */
void bt_exit_low_power_mode(void) {
    printf("Exiting low power mode\n");
    // Restore normal operation
}

/**
 * Set RF channel (no real effect on our implementation)
 */
void bt_set_rf_channel(uint8_t channel) {
    printf("Switching to channel %d (simulated)\n", channel);
}

/**
 * Get last RSSI value
 */
signal_strength_t bt_get_last_rssi(void) {
    return bt_last_rssi;
}

/**
 * Get temperature (MacBook doesn't expose CPU temp easily)
 */
int16_t bt_get_temperature(void) {
    return 20 * 10; // 20°C fixed value
}

/**
 * Get random byte
 */
uint8_t bt_get_random_byte(void) {
    return rand() & 0xFF;
}

/**
 * Get GPS data (simulated)
 */
bool bt_get_gps_data(geo_location_t* location) {
    // In a real implementation, this would interface with a GPS module
    // For simulation, we'll generate random data
    
    // 80% chance of getting a GPS fix
    if (rand() % 100 < 80) {
        // Generate a random location near San Francisco for testing
        location->latitude = 37.7749 + ((double)rand() / RAND_MAX - 0.5) * 0.02;
        location->longitude = -122.4194 + ((double)rand() / RAND_MAX - 0.5) * 0.02;
        location->altitude = 10.0 + (rand() % 200);
        location->speed = (float)(rand() % 10);
        location->course = (float)(rand() % 360);
        location->hdop = 1.0f + ((float)rand() / RAND_MAX) * 2.0f;
        location->satellites = 6 + (rand() % 8);
        location->timestamp = bt_get_time_ms();
        location->fix_quality = GPS_FIX_GPS;
        
        return true;
    } else {
        // No GPS fix
        location->fix_quality = GPS_FIX_NONE;
        location->satellites = 0;
        location->timestamp = bt_get_time_ms();
        
        return false;
    }
}

/**
 * Create HAL structure with Bluetooth functions
 */
hal_t create_bt_hal(void) {
    hal_t hal;
    
    hal.init_radio = bt_init_radio;
    hal.send_packet = bt_send_packet;
    hal.receive_packet = bt_receive_packet;
    hal.sleep_ms = bt_sleep_ms;
    hal.get_time_ms = bt_get_time_ms;
    hal.get_battery_level = bt_get_battery_level;
    hal.set_led = bt_set_led;
    hal.get_button_state = bt_get_button_state;
    hal.enter_low_power_mode = bt_enter_low_power_mode;
    hal.exit_low_power_mode = bt_exit_low_power_mode;
    hal.set_rf_channel = bt_set_rf_channel;
    hal.get_last_rssi = bt_get_last_rssi;
    hal.get_temperature = bt_get_temperature;
    hal.get_random_byte = bt_get_random_byte;
    hal.get_gps_data = bt_get_gps_data;
    
    // Allocate receive buffer
    hal.receiver_buffer = malloc(MAX_MSG_SIZE);
    hal.receiver_len = 0;
    
    return hal;
}

/* ---- Main Application ---- */

// Global state
system_state_t mesh_state;
bool running = true;

// Signal handler for clean shutdown
void handle_signal(int sig) {
    printf("Received signal %d, shutting down...\n", sig);
    running = false;
}

/**
 * Main entry point
 */
int main(int argc, char **argv) {
    // Initialize random number generator
    srand(time(NULL));
    
    // Register signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Parse command line arguments
    node_id_t node_id = 0;
    if (argc > 1) {
        node_id = atoi(argv[1]);
    } else {
        // Generate a node ID based on hostname
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            // Use hash of hostname as node ID
            unsigned int hash = 0;
            for (int i = 0; hostname[i] != '\0'; i++) {
                hash = hash * 31 + hostname[i];
            }
            node_id = hash & 0xFFFF;
        } else {
            // Generate random node ID
            node_id = (rand() & 0xFFFF);
        }
    }
    
    // Use custom port if specified
    if (argc > 2) {
        bt_port = atoi(argv[2]);
    }
    
    printf("Starting mesh network with node ID %d on port %d\n", node_id, bt_port);
    
    // Create Bluetooth HAL
    hal_t hal = create_bt_hal();
    
    // Initialize mesh network
    system_init(&mesh_state, hal, node_id);
    
    // Set simulated location
    geo_location_t location;
    location.latitude = 37.7749 + ((double)rand() / RAND_MAX) * 0.01;
    location.longitude = -122.4194 + ((double)rand() / RAND_MAX) * 0.01;
    location.altitude = 10.0 + (rand() % 100);
    location.timestamp = hal.get_time_ms();
    location.fix_quality = GPS_FIX_SIMULATION;
    location.satellites = 0;
    set_location(&mesh_state, location);
    
    printf("Mesh network initialized\n");
    printf("Commands:\n");
    printf("  send <destination> <message> - Send a message to another node\n");
    printf("  broadcast <message> - Send a broadcast message to all nodes\n");
    printf("  status - Show network status\n");
    printf("  scan - Scan for new devices\n");
    printf("  ping <node_id> - Ping another node\n");
    printf("  location - Show current location\n");
    printf("  locations - Show known node locations\n");
    printf("  poi <name> <lat> <lon> <type> <desc> - Add a point of interest\n");
    printf("  zone <name> <lat> <lon> <radius> <status> <desc> - Add a rescue zone\n");
    printf("  map <tile_id> <zoom> - Request a map tile\n");
    printf("  quit - Exit\n");
    
    // Main loop
    char command[1024];
    uint32_t last_housekeeping = 0;
    uint32_t last_gps_update = 0;
    uint32_t last_location_broadcast = 0;
    
    while (running) {
        // Process mesh network tasks
        // Check for messages multiple times per cycle for better responsiveness
        for (int i = 0; i < 5; i++) {
            process_incoming_messages(&mesh_state);
            hal.sleep_ms(1); // Small sleep between checks
        }
        
        // Send beacons and location updates periodically
        uint32_t current_time = hal.get_time_ms();
        
        send_beacon(&mesh_state);
        
        // Update GPS data periodically
        if (current_time - last_gps_update >= 5000) { // Every 5 seconds
            update_gps_location(&mesh_state);
            last_gps_update = current_time;
        }
        
        // Broadcast our location periodically
        if (current_time - last_location_broadcast >= LOCATION_UPDATE_INTERVAL_MS) {
            broadcast_location(&mesh_state);
            last_location_broadcast = current_time;
        }
        
        process_queue(&mesh_state);
        
        // Periodic maintenance
        if (current_time - last_housekeeping >= 30000) { // Every 30 seconds
            clean_stale_nodes(&mesh_state);
            clean_stale_routes(&mesh_state);
            check_battery_status(&mesh_state);
            last_housekeeping = current_time;
        }
        
        // Check for user input (non-blocking)
        fd_set readfds;
        struct timeval tv;
        
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms
        
        if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0) {
            if (fgets(command, sizeof(command), stdin) != NULL) {
                // Remove newline
                command[strcspn(command, "\n")] = 0;
                
                if (strncmp(command, "send ", 5) == 0) {
                    // Format: send <destination> <message>
                    int dest;
                    char message[MAX_MSG_SIZE];
                    
                    if (sscanf(command, "send %d %[^\n]", &dest, message) == 2) {
                        printf("Sending message to node %d: %s\n", dest, message);
                        send_message(&mesh_state, dest, MSG_DATA, 
                                    (uint8_t*)message, strlen(message) + 1, PRIORITY_NORMAL);
                    } else {
                        printf("Invalid format. Use: send <destination> <message>\n");
                    }
                } else if (strncmp(command, "broadcast ", 10) == 0) {
                    // Format: broadcast <message>
                    char message[MAX_MSG_SIZE];
                    
                    if (sscanf(command, "broadcast %[^\n]", message) == 1) {
                        printf("Broadcasting message: %s\n", message);
                        send_message(&mesh_state, 0xFFFF, MSG_DATA, 
                                    (uint8_t*)message, strlen(message) + 1, PRIORITY_NORMAL);
                    } else {
                        printf("Invalid format. Use: broadcast <message>\n");
                    }
                } else if (strcmp(command, "status") == 0) {
                    // Show network status
                    printf("Node ID: %d\n", mesh_state.node_id);
                    printf("Battery: %d%%\n", mesh_state.battery_level);
                    
                    char loc_str[128];
                    format_location_string(&mesh_state.location, loc_str, sizeof(loc_str));
                    printf("Location: %s\n", loc_str);
                    
                    printf("Known nodes: %d\n", mesh_state.node_count);
                    
                    for (int i = 0; i < mesh_state.node_count; i++) {
                        printf("  Node %d: Signal: %d, Battery: %d%%, Last seen: %u ms ago\n",
                              mesh_state.known_nodes[i].id,
                              mesh_state.known_nodes[i].signal_strength,
                              mesh_state.known_nodes[i].battery_level,
                              hal.get_time_ms() - mesh_state.known_nodes[i].last_seen);
                        
                        // If we have location info for this node, display it
                        if (mesh_state.known_nodes[i].location.fix_quality != GPS_FIX_NONE) {
                            format_location_string(&mesh_state.known_nodes[i].location, 
                                                loc_str, sizeof(loc_str));
                            printf("    Location: %s\n", loc_str);
                            printf("    Distance: %.2f meters\n", 
                                 mesh_state.known_nodes[i].distance_estimate);
                        }
                    }
                    
                    printf("Routes: %d\n", mesh_state.route_count);
                    for (int i = 0; i < mesh_state.route_count; i++) {
                        printf("  To %d via %d (%d hops)\n",
                              mesh_state.routing_table[i].destination,
                              mesh_state.routing_table[i].next_hop,
                              mesh_state.routing_table[i].hop_count);
                    }
                    
                    printf("POIs: %d\n", mesh_state.map_system.poi_count);
                    for (int i = 0; i < mesh_state.map_system.poi_count; i++) {
                        poi_t* poi = &mesh_state.map_system.points_of_interest[i];
                        char loc_str[128];
                        format_location_string(&poi->location, loc_str, sizeof(loc_str));
                        printf("  %s (%s): %s\n", poi->name, loc_str, poi->description);
                    }
                    
                    printf("Rescue Zones: %d\n", mesh_state.map_system.zone_count);
                    for (int i = 0; i < mesh_state.map_system.zone_count; i++) {
                        rescue_zone_t* zone = &mesh_state.map_system.rescue_zones[i];
                        char loc_str[128];
                        format_location_string(&zone->center, loc_str, sizeof(loc_str));
                        printf("  %s (%s, radius: %.1f m): %s\n", 
                              zone->name, loc_str, zone->radius, zone->description);
                    }
                } else if (strcmp(command, "scan") == 0) {
                    // Rescan for devices
                    bt_scan_for_devices();
                } else if (strncmp(command, "ping ", 5) == 0) {
                    // Format: ping <node_id>
                    int dest;
                    
                    if (sscanf(command, "ping %d", &dest) == 1) {
                        printf("Pinging node %d\n", dest);
                        
                        // Create ping message with timestamp
                        uint32_t timestamp = hal.get_time_ms();
                        uint8_t payload[8];
                        memcpy(payload, &timestamp, sizeof(timestamp));
                        
                        send_message(&mesh_state, dest, MSG_PING, payload, sizeof(timestamp), PRIORITY_LOW);
                    } else {
                        printf("Invalid format. Use: ping <node_id>\n");
                    }
                } else if (strcmp(command, "location") == 0) {
                    // Show current location
                    update_gps_location(&mesh_state);
                    
                    char loc_str[128];
                    format_location_string(&mesh_state.location, loc_str, sizeof(loc_str));
                    printf("Current location: %s\n", loc_str);
                } else if (strcmp(command, "locations") == 0) {
                    // Show all known locations
                    printf("Known node locations:\n");
                    
                    printf("  Node %d (self): ", mesh_state.node_id);
                    char loc_str[128];
                    format_location_string(&mesh_state.location, loc_str, sizeof(loc_str));
                    printf("%s\n", loc_str);
                    
                    for (int i = 0; i < mesh_state.node_count; i++) {
                        node_info_t* node = &mesh_state.known_nodes[i];
                        if (node->location.fix_quality != GPS_FIX_NONE) {
                            format_location_string(&node->location, loc_str, sizeof(loc_str));
                            printf("  Node %d: %s\n", node->id, loc_str);
                            
                            // Calculate distance
                            float distance = haversine_distance(
                                mesh_state.location.latitude, mesh_state.location.longitude,
                                node->location.latitude, node->location.longitude);
                                
                            printf("    Distance: %.2f meters\n", distance);
                        }
                    }
                } else if (strncmp(command, "poi ", 4) == 0) {
                    // Format: poi <name> <lat> <lon> <type> <desc>
                    char name[32];
                    double lat, lon;
                    int type;
                    char desc[128];
                    
                    if (sscanf(command, "poi %31s %lf %lf %d %127[^\n]", 
                             name, &lat, &lon, &type, desc) == 5) {
                        // Create and add POI
                        poi_t poi;
                        memset(&poi, 0, sizeof(poi_t));
                        
                        strncpy(poi.name, name, sizeof(poi.name) - 1);
                        strncpy(poi.description, desc, sizeof(poi.description) - 1);
                        
                        poi.location.latitude = lat;
                        poi.location.longitude = lon;
                        poi.location.altitude = mesh_state.location.altitude;
                        poi.location.fix_quality = GPS_FIX_MANUAL;
                        poi.location.timestamp = hal.get_time_ms();
                        
                        poi.type = type;
                        poi.timestamp = hal.get_time_ms();
                        
                        // Add to our database
                        add_point_of_interest(&mesh_state, &poi);
                        
                        // Broadcast to network
                        broadcast_point_of_interest(&mesh_state, &poi);
                        
                        printf("Added and broadcast POI: %s\n", name);
                    } else {
                        printf("Invalid format. Use: poi <name> <lat> <lon> <type> <desc>\n");
                        printf("Types: 0=general, 1=shelter, 2=medical, 3=water, 4=food\n");
                    }
                } else if (strncmp(command, "zone ", 5) == 0) {
                    // Format: zone <name> <lat> <lon> <radius> <status> <desc>
                    char name[32];
                    double lat, lon;
                    float radius;
                    int status;
                    char desc[128];
                    
                    if (sscanf(command, "zone %31s %lf %lf %f %d %127[^\n]", 
                             name, &lat, &lon, &radius, &status, desc) == 6) {
                        // Create and add rescue zone
                        rescue_zone_t zone;
                        memset(&zone, 0, sizeof(rescue_zone_t));
                        
                        strncpy(zone.name, name, sizeof(zone.name) - 1);
                        strncpy(zone.description, desc, sizeof(zone.description) - 1);
                        
                        zone.center.latitude = lat;
                        zone.center.longitude = lon;
                        zone.center.altitude = mesh_state.location.altitude;
                        zone.center.fix_quality = GPS_FIX_MANUAL;
                        zone.center.timestamp = hal.get_time_ms();
                        
                        zone.radius = radius;
                        zone.status = status;
                        zone.timestamp = hal.get_time_ms();
                        
                        // Add to our database
                        add_rescue_zone(&mesh_state, &zone);
                        
                        // Broadcast to network
                        broadcast_rescue_zone(&mesh_state, &zone);
                        
                        printf("Added and broadcast rescue zone: %s\n", name);
                        
                        // Check if we're in this zone
                        if (is_in_rescue_zone(&mesh_state.location, &zone)) {
                            printf("Alert: You are currently inside this rescue zone!\n");
                        }
                    } else {
                        printf("Invalid format. Use: zone <name> <lat> <lon> <radius> <status> <desc>\n");
                        printf("Status: 0=proposed, 1=active, 2=clearing, 3=cleared\n");
                    }
                } else if (strncmp(command, "map ", 4) == 0) {
                    // Format: map <tile_id> <zoom>
                    int tile_id, zoom;
                    
                    if (sscanf(command, "map %d %d", &tile_id, &zoom) == 2) {
                        // First try to load locally
                        bool loaded = load_map_tile(&mesh_state.map_system, tile_id, zoom);
                        
                        if (!loaded) {
                            // Request from network
                            request_map_tile(&mesh_state, tile_id, zoom);
                            printf("Requested map tile %d (zoom %d) from network\n", tile_id, zoom);
                        } else {
                            printf("Loaded map tile %d (zoom %d) from local storage\n", tile_id, zoom);
                        }
                    } else {
                        printf("Invalid format. Use: map <tile_id> <zoom>\n");
                    }
                } else if (strcmp(command, "quit") == 0) {
                    running = false;
                } else {
                    printf("Unknown command\n");
                }
            }
        }
        
        // Short sleep to prevent high CPU usage
        hal.sleep_ms(10);
    }
    
    printf("Mesh network shutdown\n");
    
    // Clean up
    if (bt_socket >= 0) {
        close(bt_socket);
    }
    
    if (hal.receiver_buffer) {
        free(hal.receiver_buffer);
    }
    
    // Free map tiles
    for (int i = 0; i < mesh_state.map_system.map_tile_count; i++) {
        if (mesh_state.map_system.map_tiles[i].data) {
            free(mesh_state.map_system.map_tiles[i].data);
        }
    }
    
    return 0;
}