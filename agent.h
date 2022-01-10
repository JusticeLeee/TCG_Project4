/**
 * Framework for NoGo and similar games (C++ 11)
 * agent.h: Define the behavior of variants of the player
 *
 * Author: Theory of Computer Games (TCG 2021)
 *         Computer Games and Intelligence (CGI) Lab, NYCU, Taiwan
 *         https://cgilab.nctu.edu.tw/
 */

#pragma once
#include <string>
#include <random>
#include <sstream>
#include <map>
#include <type_traits>
#include <algorithm>
#include "board.h"
#include "action.h"
#include <fstream>
#include <unistd.h>
#include <ctime>
#include <thread>
// std::ostream& //debug = *(new std::ofstream);
// std::ostream& //debug = //std::cout;

class agent {
public:
	agent(const std::string& args = "") {
		std::stringstream ss("name=unknown role=unknown mcts N=1000 c=1 timer=n choose=visit_count cond=0 num_worker=1 " + args);
		for (std::string pair; ss >> pair; ) {
			std::string key = pair.substr(0, pair.find('='));
			std::string value = pair.substr(pair.find('=') + 1);
			meta[key] = { value };
		}
	}
	virtual ~agent() {}
	virtual void open_episode(const std::string& flag = "") {}
	virtual void close_episode(const std::string& flag = "") {}
	virtual action take_action(const board& b) { return action(); }
	virtual bool check_for_win(const board& b) { return false; }

public:
	virtual std::string property(const std::string& key) const { return meta.at(key); }
	virtual void notify(const std::string& msg) { meta[msg.substr(0, msg.find('='))] = { msg.substr(msg.find('=') + 1) }; }
	virtual std::string name() const { return property("name"); }
	virtual std::string role() const { return property("role"); }


protected:
	typedef std::string key;
	struct value {
		std::string value;
		operator std::string() const { return value; }
		template<typename numeric, typename = typename std::enable_if<std::is_arithmetic<numeric>::value, numeric>::type>
		operator numeric() const { return numeric(std::stod(value)); }
	};
	std::map<key, value> meta;
};

/**
 * base agent for agents with randomness
 */
class random_agent : public agent {
public:
	random_agent(const std::string& args = "") : agent(args) {
		if (meta.find("seed") != meta.end())
			engine.seed(int(meta["seed"]));
	}
	virtual ~random_agent() {}

protected:
	std::default_random_engine engine;
};

/**
 * random player for both side
 * put a legal piece randomly
 */
class player : public random_agent {
public:
	player(const std::string& args = "") :random_agent("name=random role=unknown " + args),
		space(board::size_x * board::size_y), opp_space(board::size_x * board::size_y), who(board::empty) {
		if (name().find_first_of("[]():; ") != std::string::npos)
			throw std::invalid_argument("invalid name: " + name());
		if (role() == "black") who = board::black;
		if (role() == "white") who = board::white;
		if (who == board::empty)
			throw std::invalid_argument("invalid role: " + role());
		for (size_t i = 0; i < space.size(); i++){
			space[i] = action::place(i, who);
			//create opponet space
			if(who == board::black) opp_space[i] = action::place(i, board::white);
			if(who == board::white) opp_space[i] = action::place(i, board::black);
		}
		simulation_count = stoi(property("N"));
		num_worker = stoi(property("num_worker"));
		weight = stof(property("c"));
		timer = property("timer");
		choose = property("choose");
		cond = property("cond");
		for(int i = 0 ; i < num_worker ;i++){
			thread_space[i] = space;
			thread_opp_space[i] = opp_space;
		}
	}
	virtual void close_episode(const std::string& flag = "") {step = 0;}
	virtual action take_action(const board& state) {
		std::thread threads[num_worker];
		node* roots[num_worker];
		
		std::clock_t start = std::chrono::high_resolution_clock::now().time_since_epoch().count(); // get current time
		for(int i = 0 ;i<num_worker; i ++){
			roots[i] = new_node(state);
			threads[i] = std::thread(&player::build_tree,this,roots[i],state,i,step);
			// std::cout<<"i: "<<i<<"cost time:"<<(std::clock()-start)/ (double) CLOCKS_PER_SEC<<std::endl;
		}
		step++;
		for(int i = 0 ;i<num_worker; i ++) {
			threads[i].join();
			// std::cout<<"cost time:"<<(std::chrono::high_resolution_clock::now().time_since_epoch().count()-start)/ (double) CLOCKS_PER_SEC<<std::endl;
		}
		// std::cout<<"Total wait cost time:"<<(std::chrono::high_resolution_clock::now().time_since_epoch().count()-start)/ (double) CLOCKS_PER_SEC<<std::endl;
		
		//collect best index
		int best_index[num_worker]={0};
		for(int n = 0 ; n<num_worker ; n++){
			int index = -1;
			float max=-100;
			for(size_t i = 0 ; i <roots[n]->childs.size(); i++){
				if(roots[n]->childs[i]->visit_count>max){
					max = roots[n]->childs[i]->visit_count;
					index = i ;
				}
			}
			best_index[n] = index;
		}

		std::map<action::place, int> action_map_visit;
		for(int n = 0; n < num_worker ;n++){
			for (const action::place& move : space) {
				board after = state;
				if (move.apply(after) == board::legal){
					if(after == roots[n]->childs[best_index[n]]->state){
						if(choose=="visit_count")
							action_map_visit[move]+=roots[n]->childs[best_index[n]]->visit_count;
						else{
							action_map_visit[move]+=1;
						}
					}
				}
			}
		}
		//get best_move
		action::place best_move = action();
		float max = -100;
		for(auto action : action_map_visit){
			// std::cout<<"action : "<<action.first<<"value :"<<action.second<<std::endl;
			if(action.second>max){
				max = action.second;
				best_move = action.first;
			}
		}

		for(int n = 0 ; n< num_worker ; n++) delete_node(roots[n]);
		return best_move;
	}

	struct node{
		board state;
		float visit_count;
		float win_count;
		float uct_value;
		size_t child_visit_count;
		std::vector<node*> childs;
	};

	void build_tree(struct node* root,const board& state, int n,float current_step){		
		if(timer=="y"){
			float time_limit = 1;
			// float time_limit = 15 - 0.44*current_step;
			std::cout<<"time_limit :"<<time_limit<<std::endl;
			std::clock_t start = std::chrono::high_resolution_clock::now().time_since_epoch().count();// get current time
			while(1){
				my_turn[n] = true;
				update_nodes[n].push_back(root);
				insert(root,state,n);
				if( (std::chrono::high_resolution_clock::now().time_since_epoch().count()-start)/ (double) CLOCKS_PER_SEC > time_limit*1000) {
					std::cout<<"total_count: "<<total_count[n]<<std::endl;
					total_count[n] = 0 ;
					break;
				}
			}
		}

		else if(timer=="n"){
			int total_count_ = 0;
			while(total_count_<simulation_count){
				my_turn[n] = true;
				update_nodes[n].push_back(root);
				insert(root,state,n);
				total_count_++;
				// std::cout<<total_count_<<std::endl;
			}
		}
	}
	void delete_node(struct node * root){
		for(size_t i = 0 ; i<root->childs.size(); i++)
			delete_node(root->childs[i]);
		delete(root);
	}
	bool simulation(struct node * current_node,int n){
		board after = current_node->state;
		bool end = false;
		bool win = true;
		int count = 0 ;
		int temp = 0;

		if(my_turn[n]==true) {
			win = false;
			count = 0;
		}
		else {
			win = true;
			count = 1;
		}
		std::shuffle(thread_space[n].begin(), thread_space[n].end(), engine);
		std::shuffle(thread_opp_space[n].begin(), thread_opp_space[n].end(), engine);
		int space_idx = 0, opp_space_idx = 0;

		while(!end){
			bool exist_legal_move = false;
			if(count %2 == 0 ){// my move
				// std::shuffle(thread_space[n].begin(), thread_space[n].end(), engine);
				// for (const action::place& move : thread_space[n]) {
				while(space_idx<=80){
					const action::place& move = thread_space[n][space_idx++];
					if (move.apply(after) == board::legal){
						// debug<<"count ==0 have legal move"<<std::endl;
						win = true;
						exist_legal_move = true;
						count++; 
						break;
					}
				}
			}
			else if(count %2 == 1 ) {// opponent move
				// std::shuffle(thread_opp_space[n].begin(), thread_opp_space[n].end(), engine);
				// for (const action::place& move : thread_opp_space[n]) {
				while(opp_space_idx<=80){
					const action::place& move = thread_opp_space[n][opp_space_idx++];
					if (move.apply(after) == board::legal){
						//debug<<"count ==1 have legal move"<<std::endl;
						win = false;
						exist_legal_move = true;
						count++; 
						break;
					}
				}
			}
			if(!exist_legal_move) {
				end = true;
			}
		}
		total_count[n]++;
		// std::cout<<"simulation_end"<<std::endl;
		return win;
	}

	struct node* new_node(board state){
		struct node* current_node = new struct node;
		current_node->visit_count = 0;
		current_node->win_count = 0;
		current_node->uct_value = 10000;
		current_node->child_visit_count =0;
		current_node->state = state;
		return current_node;
	}

	void insert(struct node* root, board state, int n){
		// collect child
		if(root->childs.size()==0){
			if(my_turn[n]==true){
				for (const action::place& move : thread_space[n]) {
					board after = state;
					if (move.apply(after) == board::legal){
						struct node * current_node = new_node(after);		
						root->childs.push_back(current_node);
					}
				}
			}
			else {
				for (const action::place& move : thread_opp_space[n]) {
					board after = state;
					if (move.apply(after) == board::legal){
						struct node * current_node = new_node(after);		
						root->childs.push_back(current_node);
					}
				}
			}
		}
		// do simulation
		if(root->visit_count == 0) {
			bool win = simulation(root, n);
			update(win,n);
		}
		else {
			int index = -1;
			float max=-100;
			bool do_expand = true;
			// check need expand or not
			if(root->child_visit_count == root->childs.size()) do_expand = false;
			// std::cout<<"root->child_visit_count"<<root->child_visit_count<<std::endl;
			if(root->childs.size()==0){
				bool win = simulation(root, n);
				update(win,n);
				return;
			} 

			if(do_expand){
				std::shuffle(root->childs.begin(), root->childs.end(), engine);
				// std::cout<<"root->childs[0]->uct_value"<<root->childs[0]->uct_value<<std::endl;
				for(size_t i = 0 ; i<root->childs.size(); i++){
					if(root->childs[i]->uct_value>max && root->childs[i]->visit_count==0){
						max = root->childs[i]->uct_value;
						index = i;
						root->child_visit_count++;
					}
				}
				// std::cout<<"expand index :"<<index<<std::endl;
			}else{
				for(size_t i = 0 ; i<root->childs.size(); i++){
					if(root->childs[i]->uct_value>max){
						max = root->childs[i]->uct_value;
						index = i;
					}
				}
				// std::cout<<"select index:"<<index<<std::endl;
			}
			my_turn[n] = !my_turn[n];
			update_nodes[n].push_back(root->childs[index]);
			insert(root->childs[index],root->childs[index]->state,n);
		}
	}

	float UCT_value(float win_count, float visit_count, int n){
		return  win_count/visit_count + weight * log(total_count[n]) / visit_count ;
	}

	void update(bool win, int n){
		// std::cout<<"update: "<<n<<std::endl; 
		// std::cout<<"update_nodes[n].size() ;"<<update_nodes[n].size()<<std::endl;
		float value = 0;
		if(win) value = 1;
		for (size_t i = 0 ; i< update_nodes[n].size() ; i++){
			update_nodes[n][i]->visit_count++;
			update_nodes[n][i]->win_count += value;		
			// if(cond=="op_best"){	
			if(i%2==1)
				update_nodes[n][i]->uct_value = UCT_value(update_nodes[n][i]->win_count, update_nodes[n][i]->visit_count, n);
			else
				update_nodes[n][i]->uct_value = UCT_value(update_nodes[n][i]->visit_count-update_nodes[n][i]->win_count, update_nodes[n][i]->visit_count, n);
			// }else{
			// 	update_nodes[n][i]->uct_value = UCT_value(update_nodes[n][i]->win_count, update_nodes[n][i]->visit_count, n);
			// }
		}
		// clear total_count and update_nodes[n]
		update_nodes[n].clear();
	}
	int step = 0;
	int total_count[100];
	std::vector<node*> update_nodes[100];
	bool my_turn[100];
private:
	int simulation_count;
	int num_worker;
	float weight;
	std::string choose;
	std::string timer;
	std::string cond;
	std::vector<action::place> space;
	std::vector<action::place> thread_space[100];
	std::vector<action::place> opp_space;
	std::vector<action::place> thread_opp_space[100];
	board::piece_type who;
};


